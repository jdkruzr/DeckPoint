#include "ChapterXPathResolver.h"

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Logging.h>
#include <Print.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
std::string stripPrefix(const XML_Char* name) {
  if (!name) {
    return "";
  }

  const char* local = std::strrchr(name, ':');
  return local ? std::string(local + 1) : std::string(name);
}

struct NameCounter {
  std::string name;
  int count;
};

struct ParentState {
  std::vector<NameCounter> children;

  int nextIndex(const std::string& name) {
    for (auto& child : children) {
      if (child.name == name) {
        child.count++;
        return child.count;
      }
    }

    children.push_back({name, 1});
    return 1;
  }
};

struct PathSegment {
  std::string name;
  int index;
};

std::string buildParagraphXPath(const int spineIndex, const std::vector<PathSegment>& path, const int textNodeIndex,
                                const size_t charOffset) {
  std::string xpath = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  for (const auto& segment : path) {
    xpath += "/" + segment.name + "[" + std::to_string(segment.index) + "]";
  }
  if (textNodeIndex > 0) {
    xpath += "/text()[" + std::to_string(textNodeIndex) + "]." + std::to_string(charOffset);
  }
  return xpath;
}

class XPathParagraphResolver final : public Print {
 public:
  explicit XPathParagraphResolver(const int targetParagraph) : targetParagraph(targetParagraph) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "Failed to create XML parser");
      return;
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &XPathParagraphResolver::startElement, &XPathParagraphResolver::endElement);
  }

  ~XPathParagraphResolver() override { destroyXmlParser(parser); }

  bool ok() const { return parser != nullptr && parseOk; }

  bool finish() {
    if (!parser || !parseOk || stopped) {
      return parseOk;
    }

    if (XML_Parse(parser, "", 0, XML_TRUE) == XML_STATUS_ERROR) {
      LOG_ERR("KOX", "Final XML parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      parseOk = false;
    }
    return parseOk;
  }

  bool hasMatch() const { return !xpath.empty(); }
  const std::string& getXPath() const { return xpath; }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!parser || !parseOk || stopped) {
      return size;
    }

    if (XML_Parse(parser, reinterpret_cast<const char*>(buffer), static_cast<int>(size), XML_FALSE) != XML_STATUS_OK) {
      const enum XML_Error error = XML_GetErrorCode(parser);
      if (error != XML_ERROR_ABORTED) {
        LOG_ERR("KOX", "XML parse error: %s", XML_ErrorString(error));
        parseOk = false;
      }
    }

    return size;
  }

  int spineIndex = 0;

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<XPathParagraphResolver*>(userData);
    self->onStartElement(name);
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<XPathParagraphResolver*>(userData);
    self->onEndElement(name);
  }

  void onStartElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
        parentStates.emplace_back();
      }
      depth++;
      return;
    }

    const int siblingIndex = parentStates.back().nextIndex(name);
    path.push_back({name, siblingIndex});
    parentStates.emplace_back();

    // Count both <p> and <li> as paragraph-like positions, matching how the section
    // layout tracks them (xpathParagraphIndex and xpathListItemIndex). This ensures
    // KOReader progress in list items maps to the correct XPath.
    if (name == "p") {
      paragraphCount++;
    } else if (name == "li") {
      paragraphCount++;
    }
    if (paragraphCount == targetParagraph) {
      xpath = buildParagraphXPath(spineIndex, path, 0, 0);
      stopped = true;
      XML_StopParser(parser, XML_FALSE);
    }

    depth++;
  }

  void onEndElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      parentStates.clear();
      path.clear();
      return;
    }

    if (!path.empty()) {
      path.pop_back();
    }
    if (!parentStates.empty()) {
      parentStates.pop_back();
    }
  }

  XML_Parser parser = nullptr;
  const int targetParagraph;
  bool parseOk = true;
  bool insideBody = false;
  bool stopped = false;
  int depth = 0;
  int bodyDepth = -1;
  int paragraphCount = 0;
  std::vector<ParentState> parentStates;
  std::vector<PathSegment> path;
  std::string xpath;
};

// DECKPOINT: crengine DOM text model. KOReader XPointers address text nodes of the
// DOM that crengine (github.com/koreader/crengine, crengine/src) builds, which differs
// from the raw XML that the layout's visible offsets count:
//   - lvxml.cpp LVXMLParser::ReadText: a text node ends at the next '<' (element,
//     comment, PI, CDATA); nodes are never merged.
//   - lvxml.cpp PreProcessXmlString: outside white-space:pre, \r \n \t become ' ' and
//     runs of spaces collapse to the first one. Entities decode to one codepoint.
//     Inside pre, tabs expand to 8-column stops (ExpandTabs) and a newline right
//     after <pre>/<textarea> is stripped (ldomElementWriter::onText).
//   - lvtinydom.cpp ldomElementWriter::onText: a whitespace-only text that would be
//     the first child of a block element is dropped.
//   - lvtinydom.cpp autoboxChildren / whitespace compaction: in a block that also has
//     block children, whitespace-only runs survive only between two inline siblings.
//   - lvtinydom.cpp ldomXPointer::toStringV2 / createXPointerV2: "name[n]" counts
//     same-name element siblings, "text()[k]" counts text siblings, ".c" is a
//     character offset into the (collapsed) text node; boxing elements crengine
//     inserts (autoBoxing, floatBox, ...) are transparent.
// Element display defaults follow crengine/include/fb2def.h (book CSS is not
// consulted). Not modelled: crengine's HTML5 restructuring of invalid nesting (a
// block start auto-closing an open <p>), CSS white-space:pre on other elements,
// and the 8192-character text-node split in ReadText.
bool isCreBlockElement(const std::string_view name) {
  static constexpr const char* kBlocks[] = {
      "body",       "hr",     "form",    "pre",     "blockquote", "div",      "h1",       "h2",      "h3",
      "h4",         "h5",     "h6",      "p",       "output",     "section",  "ol",       "ul",      "li",
      "dl",         "dt",     "dd",      "table",   "caption",    "colgroup", "col",      "thead",   "tbody",
      "tfoot",      "tr",     "th",      "td",      "address",    "article",  "aside",    "canvas",  "fieldset",
      "figcaption", "figure", "footer",  "header",  "hgroup",     "legend",   "main",     "nav",     "noscript",
      "video",      "center", "dir",     "menu",    "multicol",   "noframes", "search",   "listing", "textarea",
      "plaintext",  "xmp",    "details", "dialog",  "summary",    "frame",    "frameset", "iframe",  "noembed",
      "template",   "select", "button",  "marquee", "applet",     "optgroup", "datalist", "map",     "area",
      "track",      "input",  "keygen",  "param",   "audio",      "source",   "title"};
  for (const char* block : kBlocks) {
    if (name == block) return true;
  }
  return false;
}

bool isCrePreElement(const std::string_view name) {
  return name == "pre" || name == "listing" || name == "textarea" || name == "plaintext" || name == "xmp";
}

bool isXmlSpace(const uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'; }

std::string lowerLocalName(const XML_Char* rawName) {
  std::string name = stripPrefix(rawName);
  for (auto& c : name) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return name;
}

struct XPointerStep {
  std::string name;
  int index;  // 0 when the step had no [n]: crengine then takes the first match
};

struct ParsedXPointer {
  std::vector<XPointerStep> steps;  // element steps below DocFragment's <body>
  int textIndex = 0;                // k of text()[k]; 0 for an element point
  uint32_t offset = 0;
};

// Accepts every toStringV1/V2 spelling: "/body/DocFragment[3]/body/div/p[2]/text().5",
// "/body[1]/DocFragment[3]/body[1]/div[1]/p[2]/text()[1].5", element points ".../img.0".
bool parseXPointer(const std::string& xp, ParsedXPointer& out) {
  size_t pos = xp.find("DocFragment");
  if (pos == std::string::npos) return false;
  pos = xp.find('/', pos);
  if (pos == std::string::npos || xp.compare(pos, 5, "/body") != 0) return false;
  pos += 5;
  if (pos < xp.size() && xp[pos] == '[') {
    pos = xp.find(']', pos);
    if (pos == std::string::npos) return false;
    pos++;
  }
  while (pos < xp.size() && xp[pos] == '/') {
    const size_t start = pos + 1;
    size_t end = start;
    while (end < xp.size() && xp[end] != '/' && xp[end] != '.' && xp[end] != '[') end++;
    std::string name = xp.substr(start, end - start);
    if (name.empty()) return false;
    int index = 0;
    if (end < xp.size() && xp[end] == '[') {
      const size_t close = xp.find(']', end);
      if (close == std::string::npos || close == end + 1) return false;
      for (size_t i = end + 1; i < close; i++) {
        if (xp[i] < '0' || xp[i] > '9') return false;
        index = index * 10 + (xp[i] - '0');
      }
      end = close + 1;
    }
    pos = end;
    if (name == "text()") {
      out.textIndex = index > 0 ? index : 1;
      break;
    }
    for (auto& c : name) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    out.steps.push_back({std::move(name), index});
  }
  if (pos < xp.size()) {
    if (xp[pos] != '.' || pos + 1 >= xp.size()) return false;
    uint32_t offset = 0;
    for (size_t i = pos + 1; i < xp.size(); i++) {
      if (xp[i] < '0' || xp[i] > '9') return false;
      offset = offset * 10 + static_cast<uint32_t>(xp[i] - '0');
    }
    out.offset = offset;
  }
  return true;
}

// Streams one chapter through expat, tracking both the layout's visible offset
// (every codepoint of character data in <body> outside non-visible elements, with
// HTML entities expanded exactly as ChapterHtmlSlimParser::defaultHandlerExpand does)
// and crengine's text nodes. One walker instance answers one query.
class CreTextWalker final : public Print {
 public:
  enum class Mode : uint8_t { Count, Forward, Reverse };

  explicit CreTextWalker(const Mode mode) : mode(mode) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "Failed to create XML parser");
      return;
    }
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &CreTextWalker::startElement, &CreTextWalker::endElement);
    XML_SetCharacterDataHandler(parser, &CreTextWalker::characterData);
    XML_SetCommentHandler(parser, &CreTextWalker::comment);
    XML_SetProcessingInstructionHandler(parser, &CreTextWalker::processingInstruction);
    XML_SetCdataSectionHandler(parser, &CreTextWalker::cdataBoundary, &CreTextWalker::cdataBoundary);
    XML_SetDefaultHandlerExpand(parser, &CreTextWalker::defaultHandler);
  }

  ~CreTextWalker() override { destroyXmlParser(parser); }

  // Forward: the XPointer of visible offset `target` (bias picks char vs. range end).
  void setForwardTarget(const uint32_t target, const KOReaderXPointer::Bias bias, const KOReaderXPointer::Style style,
                        const int spineIndex, const int spineCount) {
    forwardBias = bias;
    xpStyle = style;
    this->spineIndex = spineIndex;
    this->spineCount = spineCount;
    if (bias == KOReaderXPointer::Bias::End) {
      if (target == 0) {
        noTarget = true;
        return;
      }
      targetChar = target - 1;
    } else {
      targetChar = target;
    }
  }

  // Reverse: the visible offset an XPointer names.
  void setReverseTarget(ParsedXPointer pointer, const bool relaxFirstStep) {
    reverse = std::move(pointer);
    relaxed = relaxFirstStep;
    matchedFrameIndex.assign(reverse.steps.size(), -1);
  }

  bool ok() const { return parser != nullptr && parseOk; }

  bool finish() {
    if (!parser || !parseOk || stopped) return parseOk;
    if (XML_Parse(parser, "", 0, XML_TRUE) == XML_STATUS_ERROR) {
      LOG_ERR("KOX", "Final XML parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      parseOk = false;
    }
    if (lookahead) finishLegacy();
    return parseOk;
  }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, const size_t size) override {
    if (!parser || !parseOk || stopped || noTarget) return size;
    if (XML_Parse(parser, reinterpret_cast<const char*>(buffer), static_cast<int>(size), XML_FALSE) != XML_STATUS_OK) {
      const enum XML_Error error = XML_GetErrorCode(parser);
      if (error != XML_ERROR_ABORTED) {
        LOG_ERR("KOX", "XML parse error: %s", XML_ErrorString(error));
        parseOk = false;
      }
    }
    return size;
  }

  uint32_t totalVisibleChars() const { return raw; }
  const std::string& xpath() const { return result; }
  bool hasOffset() const { return offsetFound; }
  uint32_t offset() const { return resolvedOffset; }

 private:
  enum class ChildKind : uint8_t { None, Inline, Block };

  struct Frame {
    std::string name;
    int index = 1;
    bool block = false;
    bool pre = false;
    bool stripLeadingNewline = false;
    bool hasChildren = false;  // any DOM child at write time (crengine's getChildCount() > 0)
    bool hasBlockChild = false;
    ChildKind lastChild = ChildKind::None;  // last non-whitespace-run child
    int keptTexts = 0;
    int pendingWs = 0;  // whitespace-only text nodes whose survival depends on the next sibling
    uint32_t pendingRawStart = 0;
    std::vector<NameCounter> childCounts;
    int matchedStep = -1;
    bool onPath = false;
  };

  struct TextNode {
    bool active = false;
    bool committed = false;  // known to survive (non-whitespace, or pre)
    bool first = true;
    bool prevSpace = false;
    bool hasTarget = false;
    bool haveRawAtOffset = false;
    int index = 0;
    uint16_t column = 0;
    uint32_t rawStart = 0;
    uint32_t normLen = 0;
    uint32_t targetNorm = 0;
    uint32_t rawAtOffset = 0;
  };

  XML_Parser parser = nullptr;
  const Mode mode;
  bool parseOk = true;
  bool stopped = false;
  bool noTarget = false;
  bool insideBody = false;
  uint16_t nonVisibleDepth = 0;
  uint32_t raw = 0;
  std::vector<Frame> stack;
  TextNode node;

  // Forward state
  KOReaderXPointer::Bias forwardBias = KOReaderXPointer::Bias::Start;
  KOReaderXPointer::Style xpStyle = KOReaderXPointer::Style::Compat;
  int spineIndex = 0;
  int spineCount = 1;
  uint32_t targetChar = UINT32_MAX;
  bool emitted = false;
  bool snapForward = false;
  std::string result;
  // Legacy-style lookahead: does a later same-name sibling / later text node exist?
  bool lookahead = false;
  std::vector<PathSegment> emittedPath;
  std::vector<bool> laterSameName;
  int emittedTextIndex = 0;
  uint32_t emittedOffset = 0;
  bool laterText = false;
  size_t textParentIndex = 0;

  // Reverse state
  ParsedXPointer reverse;
  bool relaxed = false;
  size_t matched = 0;
  int relaxedCount = 0;
  std::vector<int> matchedFrameIndex;
  bool targetNodeActive = false;
  bool offsetFound = false;
  uint32_t resolvedOffset = 0;

  void stop() {
    stopped = true;
    XML_StopParser(parser, XML_FALSE);
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    static_cast<CreTextWalker*>(userData)->onStartElement(name);
  }
  static void XMLCALL endElement(void* userData, const XML_Char*) {
    static_cast<CreTextWalker*>(userData)->onEndElement();
  }
  static void XMLCALL characterData(void* userData, const XML_Char* data, const int len) {
    static_cast<CreTextWalker*>(userData)->onCharacterData(data, len);
  }
  static void XMLCALL comment(void* userData, const XML_Char*) { static_cast<CreTextWalker*>(userData)->flushText(); }
  static void XMLCALL processingInstruction(void* userData, const XML_Char*, const XML_Char*) {
    static_cast<CreTextWalker*>(userData)->flushText();
  }
  static void XMLCALL cdataBoundary(void* userData) { static_cast<CreTextWalker*>(userData)->flushText(); }
  static void XMLCALL defaultHandler(void* userData, const XML_Char* s, const int len) {
    // Same expansion as ChapterHtmlSlimParser::defaultHandlerExpand, so offsets agree.
    if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
      const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
      if (utf8Value) {
        characterData(userData, utf8Value, static_cast<int>(strlen(utf8Value)));
      } else {
        characterData(userData, s, len);
      }
    }
  }

  bool isReverse() const { return mode == Mode::Reverse; }
  bool reverseTargetOpen() const { return isReverse() && matched == reverse.steps.size(); }
  size_t reverseTargetFrame() const {
    return reverse.steps.empty() ? 0 : static_cast<size_t>(matchedFrameIndex.back());
  }
  bool inReverseTargetFrame() const { return reverseTargetOpen() && stack.size() - 1 == reverseTargetFrame(); }

  void foundOffset(const uint32_t value) {
    resolvedOffset = value;
    offsetFound = true;
    stop();
  }

  void onStartElement(const XML_Char* rawName) {
    if (stopped) return;
    const std::string name = lowerLocalName(rawName);
    if (!insideBody) {
      if (name != "body") return;
      insideBody = true;
      Frame body;
      body.name = "body";
      body.block = true;
      stack.push_back(std::move(body));
      if (reverseTargetOpen() && reverse.textIndex == 0) foundOffset(raw);
      return;
    }
    if (stack.empty()) return;
    flushText();
    if (stopped) return;

    Frame& parent = stack.back();
    int index = 1;
    bool counted = false;
    for (auto& counter : parent.childCounts) {
      if (counter.name == name) {
        index = ++counter.count;
        counted = true;
        break;
      }
    }
    if (!counted) parent.childCounts.push_back({name, 1});

    const bool block = isCreBlockElement(name);
    resolvePending(parent, block ? ChildKind::Block : ChildKind::Inline);
    if (stopped) return;
    parent.hasChildren = true;
    parent.hasBlockChild = parent.hasBlockChild || block;
    parent.lastChild = block ? ChildKind::Block : ChildKind::Inline;

    if (lookahead) {
      const size_t parentIndex = stack.size() - 1;
      if (parent.onPath && parentIndex < emittedPath.size() && emittedPath[parentIndex].name == name) {
        laterSameName[parentIndex] = true;
        maybeFinishLegacy();
        if (stopped) return;
      }
    }

    if (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) nonVisibleDepth++;

    Frame frame;
    frame.name = name;
    frame.index = index;
    frame.block = block;
    frame.pre = parent.pre || isCrePreElement(name);
    frame.stripLeadingNewline = name == "pre" || name == "textarea";
    stack.push_back(std::move(frame));

    if (isReverse() && matched < reverse.steps.size()) {
      const auto& step = reverse.steps[matched];
      const int frameIndex = static_cast<int>(stack.size() - 1);
      const bool firstStepRelaxed = matched == 0 && relaxed;
      const int expectedParent = matched == 0 ? 0 : matchedFrameIndex[matched - 1];
      if (name == step.name && (firstStepRelaxed || frameIndex == expectedParent + 1)) {
        const int siblingIndex = firstStepRelaxed ? ++relaxedCount : index;
        if (siblingIndex == (step.index > 0 ? step.index : 1)) {
          stack.back().matchedStep = static_cast<int>(matched);
          matchedFrameIndex[matched] = frameIndex;
          matched++;
          if (reverseTargetOpen() && reverse.textIndex == 0) foundOffset(raw);
        }
      }
    }
  }

  void onEndElement() {
    if (stopped || !insideBody || stack.empty()) return;
    flushText();
    if (stopped) return;
    Frame& frame = stack.back();
    resolvePending(frame, ChildKind::None);
    if (stopped) return;

    if (stack.size() == 1) {
      insideBody = false;
      stack.clear();
      nonVisibleDepth = 0;
      if (lookahead) finishLegacy();
      return;
    }
    if (nonVisibleDepth > 0) nonVisibleDepth--;

    if (frame.matchedStep >= 0) {
      if (reverseTargetOpen() && !offsetFound) {
        // The addressed element closed without the addressed text node.
        stop();
        return;
      }
      matched = static_cast<size_t>(frame.matchedStep);
    }
    if (lookahead && stack.size() - 1 == textParentIndex) {
      // Text siblings of the emitted node are settled once its parent closes.
      textParentIndex = SIZE_MAX;
    }
    stack.pop_back();
    if (lookahead) maybeFinishLegacy();
  }

  void onCharacterData(const XML_Char* data, const int len) {
    if (stopped || !insideBody || stack.empty() || nonVisibleDepth > 0 || len <= 0) return;
    const auto* ptr = reinterpret_cast<const unsigned char*>(data);
    const auto* end = ptr + len;
    if (mode == Mode::Count) {
      while (ptr < end) {
        utf8NextCodepoint(&ptr);
        raw++;
      }
      return;
    }
    while (ptr < end && !stopped) {
      processCodepoint(utf8NextCodepoint(&ptr));
    }
  }

  void processCodepoint(const uint32_t cp) {
    Frame& frame = stack.back();
    if (!node.active) {
      node = TextNode{};
      node.active = true;
      node.rawStart = raw;
    }

    const bool space = isXmlSpace(cp);
    uint32_t norm = node.normLen;
    uint32_t width = 1;
    if (frame.pre) {
      if (node.first && cp == '\n' && frame.stripLeadingNewline && !frame.hasChildren) {
        width = 0;
      } else if (cp == '\t') {
        width = 8u - (node.column & 7u);
        node.column = static_cast<uint16_t>(node.column + width);
      } else {
        node.column = cp == '\n' ? 0 : static_cast<uint16_t>(node.column + 1);
      }
      if (!node.committed) commitNode(frame);
    } else if (space) {
      if (node.prevSpace) {
        width = 0;
        norm = node.normLen - 1;
      }
      node.prevSpace = true;
    } else {
      node.prevSpace = false;
      if (!node.committed) commitNode(frame);
    }
    node.first = false;
    if (stopped) return;

    if (mode == Mode::Forward && !emitted) {
      if (snapForward && node.committed) {
        emit(node.index, norm);
      } else if (raw == targetChar) {
        const uint32_t position =
            forwardBias == KOReaderXPointer::Bias::Start ? norm : (width > 0 ? norm + width : norm + 1);
        if (node.committed) {
          emit(node.index, position);
        } else {
          node.hasTarget = true;
          node.targetNorm = position;
        }
      }
    } else if (inReverseTargetFrame() && reverse.textIndex > 0 && !node.haveRawAtOffset) {
      const uint32_t want = reverse.offset;
      if (want == norm || (width > 1 && want > norm && want < norm + width)) {
        node.haveRawAtOffset = true;
        node.rawAtOffset = raw;
        if (targetNodeActive) foundOffset(raw);
      }
    }

    node.normLen += width;
    raw++;
  }

  void commitNode(Frame& frame) {
    resolvePending(frame, ChildKind::Inline);
    if (stopped) return;
    frame.keptTexts++;
    frame.hasChildren = true;
    frame.lastChild = ChildKind::Inline;
    node.committed = true;
    node.index = frame.keptTexts;
    onTextIndexed(node.index);
    if (stopped) return;
    if (mode == Mode::Forward && !emitted && node.hasTarget) emit(node.index, node.targetNorm);
  }

  void onTextIndexed(const int index) {
    if (lookahead && stack.size() - 1 == textParentIndex && index > emittedTextIndex) {
      laterText = true;
      maybeFinishLegacy();
      return;
    }
    if (inReverseTargetFrame() && index == reverse.textIndex) {
      targetNodeActive = true;
      if (node.active && node.index == index && node.haveRawAtOffset) foundOffset(node.rawAtOffset);
    }
  }

  void flushText() {
    if (!node.active || stack.empty()) return;
    Frame& frame = stack.back();
    node.active = false;
    if (!node.committed) {
      const bool dropped = frame.block && !frame.hasChildren;
      if (!dropped) {
        if (frame.pendingWs == 0) frame.pendingRawStart = node.rawStart;
        frame.pendingWs++;
        frame.hasChildren = true;
      }
      if (mode == Mode::Forward && !emitted && node.hasTarget) snapForward = true;
      return;
    }
    if (targetNodeActive && inReverseTargetFrame() && node.index == reverse.textIndex) {
      foundOffset(raw);  // offset past the node's end: clamp to its end
    }
  }

  void resolvePending(Frame& frame, const ChildKind next) {
    if (frame.pendingWs == 0) return;
    const bool kept = next == ChildKind::None ? !frame.hasBlockChild
                                              : (next == ChildKind::Inline && frame.lastChild != ChildKind::Block);
    const int count = frame.pendingWs;
    frame.pendingWs = 0;
    if (!kept) return;
    for (int i = 0; i < count; i++) {
      frame.keptTexts++;
      if (lookahead && stack.size() - 1 == textParentIndex && &frame == &stack.back() &&
          frame.keptTexts > emittedTextIndex) {
        laterText = true;
        maybeFinishLegacy();
        if (stopped) return;
      }
      if (inReverseTargetFrame() && &frame == &stack.back() && frame.keptTexts == reverse.textIndex) {
        foundOffset(frame.pendingRawStart);
        return;
      }
    }
  }

  void emit(const int textIndex, const uint32_t offset) {
    emitted = true;
    emittedPath.clear();
    emittedPath.reserve(stack.size());
    for (size_t i = 1; i < stack.size(); i++) emittedPath.push_back({stack[i].name, stack[i].index});
    emittedTextIndex = textIndex;
    emittedOffset = offset;
    if (xpStyle != KOReaderXPointer::Style::Legacy) {
      result = formatXPointer(nullptr, false);
      stop();
      return;
    }
    // Legacy spelling needs to know whether later siblings exist: keep streaming.
    lookahead = true;
    laterSameName.assign(emittedPath.size(), false);
    for (auto& f : stack) f.onPath = true;
    textParentIndex = stack.size() - 1;
    maybeFinishLegacy();
  }

  void maybeFinishLegacy() {
    if (!lookahead) return;
    if (emittedTextIndex == 1 && !laterText && textParentIndex != SIZE_MAX) return;
    for (size_t i = 0; i < emittedPath.size(); i++) {
      if (emittedPath[i].index == 1 && !laterSameName[i]) {
        // Still undecided while the parent (stack index i) is open.
        if (i < stack.size() && stack[i].onPath) return;
      }
    }
    finishLegacy();
  }

  void finishLegacy() {
    if (!lookahead) return;
    lookahead = false;
    result = formatXPointer(&laterSameName, laterText);
    stop();
  }

  std::string formatXPointer(const std::vector<bool>* later, const bool textHasLater) const {
    using KOReaderXPointer::Style;
    std::string xp;
    xp.reserve(64 + emittedPath.size() * 16);
    const bool legacy = xpStyle == Style::Legacy;
    xp += xpStyle == Style::Explicit ? "/body[1]/DocFragment[" : "/body/DocFragment";
    if (xpStyle != Style::Explicit && (!legacy || spineCount > 1)) xp += "[";
    if (xpStyle == Style::Explicit || !legacy || spineCount > 1) {
      xp += std::to_string(spineIndex + 1);
      xp += "]";
    }
    xp += xpStyle == Style::Explicit ? "/body[1]" : "/body";
    for (size_t i = 0; i < emittedPath.size(); i++) {
      xp += "/";
      xp += emittedPath[i].name;
      if (!legacy || emittedPath[i].index > 1 || (later && (*later)[i])) {
        xp += "[";
        xp += std::to_string(emittedPath[i].index);
        xp += "]";
      }
    }
    xp += "/text()";
    if (!legacy || emittedTextIndex > 1 || textHasLater) {
      xp += "[";
      xp += std::to_string(emittedTextIndex);
      xp += "]";
    }
    xp += ".";
    xp += std::to_string(emittedOffset);
    return xp;
  }
};

bool streamChapter(const std::shared_ptr<Epub>& epub, const int spineIndex, CreTextWalker& walker) {
  if (!walker.ok()) return false;
  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) return false;
  return epub->readItemContentsToStream(href, walker, 1024) && walker.finish();
}
}  // namespace

std::string ChapterXPathResolver::findXPathForParagraph(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                        const uint16_t paragraphIndex) {
  if (!epub || paragraphIndex == 0 || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  XPathParagraphResolver resolver(paragraphIndex);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved paragraph %u in spine %d -> %s", paragraphIndex, spineIndex, resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Paragraph %u not found in spine %d", paragraphIndex, spineIndex);
  return "";
}
std::string ChapterXPathResolver::findXPathForVisibleTextOffset(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                                const uint32_t visibleTextOffset,
                                                                const KOReaderXPointer::Bias bias,
                                                                const KOReaderXPointer::Style style) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  CreTextWalker walker(CreTextWalker::Mode::Forward);
  walker.setForwardTarget(visibleTextOffset, bias, style, spineIndex, epub->getSpineItemsCount());
  if (!streamChapter(epub, spineIndex, walker) || walker.xpath().empty()) {
    LOG_DBG("KOX", "Visible offset %u not found in spine %d", visibleTextOffset, spineIndex);
    return "";
  }
  LOG_DBG("KOX", "Resolved visible offset %u in spine %d -> %s", visibleTextOffset, spineIndex, walker.xpath().c_str());
  return walker.xpath();
}

std::optional<uint32_t> ChapterXPathResolver::findVisibleTextOffsetForXPath(const std::shared_ptr<Epub>& epub,
                                                                            const int spineIndex,
                                                                            const std::string& xpath,
                                                                            const bool relaxFirstStep) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return std::nullopt;
  }
  ParsedXPointer pointer;
  if (!parseXPointer(xpath, pointer) || (relaxFirstStep && pointer.steps.empty())) {
    return std::nullopt;
  }

  CreTextWalker walker(CreTextWalker::Mode::Reverse);
  walker.setReverseTarget(std::move(pointer), relaxFirstStep);
  if (!streamChapter(epub, spineIndex, walker) || !walker.hasOffset()) {
    LOG_DBG("KOX", "XPointer %s not found in spine %d", xpath.c_str(), spineIndex);
    return std::nullopt;
  }
  LOG_DBG("KOX", "Resolved %s in spine %d -> offset %u", xpath.c_str(), spineIndex, walker.offset());
  return walker.offset();
}

std::string ChapterXPathResolver::findXPathForProgress(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                       const float intraSpineProgress) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  if (!(intraSpineProgress > 0.0f)) {
    return "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }

  CreTextWalker counter(CreTextWalker::Mode::Count);
  if (!streamChapter(epub, spineIndex, counter)) {
    return "";
  }
  const uint32_t totalVisibleChars = counter.totalVisibleChars();
  if (totalVisibleChars == 0) {
    return "";
  }

  const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
  const uint32_t target = std::max<uint32_t>(
      1,
      std::min(totalVisibleChars, static_cast<uint32_t>(std::ceil(clamped * static_cast<float>(totalVisibleChars)))));
  // The position just past the target-th visible character.
  const std::string xpath = findXPathForVisibleTextOffset(epub, spineIndex, target, KOReaderXPointer::Bias::End);
  if (!xpath.empty()) {
    LOG_DBG("KOX", "Resolved progress %.3f in spine %d -> %s", intraSpineProgress, spineIndex, xpath.c_str());
  }
  return xpath;
}
