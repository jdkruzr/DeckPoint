#pragma once

// DECKPOINT: test-only reference model of the DOM KOReader's crengine builds from
// an XHTML chapter, written tree-first after the crengine sources it mirrors
// (github.com/koreader/crengine, crengine/src):
//   - lvxml.cpp LVXMLParser::ReadText: a text node runs until the next '<'
//     (element, comment, PI, CDATA), so markup always splits text nodes.
//   - lvxml.cpp PreProcessXmlString: outside white-space:pre, \r \n \t become ' '
//     and runs of spaces collapse to one; entities decode to their codepoint(s).
//   - lvtinydom.cpp ldomElementWriter::onText: a whitespace-only text that would
//     be the first child of a block element is dropped.
//   - lvtinydom.cpp autoboxChildren / the whitespace compaction pass: in a block
//     that also has block children, whitespace-only runs survive only between two
//     inline siblings.
//   - lvtinydom.cpp createXPointerV2 / getNodeByIndex: "name[n]" is the n-th
//     element child of that name, "text()[k]" the k-th text child, ".c" a
//     character offset into that text node (index 1 when omitted).
// The raw stream mirrors ChapterHtmlSlimParser's visible-offset count.

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <expat.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace credomref {

inline std::string lowerLocal(const char* name) {
  const char* local = std::strrchr(name, ':');
  std::string out = local ? local + 1 : name;
  for (auto& c : out) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return out;
}

inline bool isBlockTag(const std::string& n) {
  static const char* const kBlocks[] = {
      "body",       "hr",     "form",    "pre",     "blockquote", "div",      "h1",       "h2",      "h3",
      "h4",         "h5",     "h6",      "p",       "output",     "section",  "ol",       "ul",      "li",
      "dl",         "dt",     "dd",      "table",   "caption",    "colgroup", "col",      "thead",   "tbody",
      "tfoot",      "tr",     "th",      "td",      "address",    "article",  "aside",    "canvas",  "fieldset",
      "figcaption", "figure", "footer",  "header",  "hgroup",     "legend",   "main",     "nav",     "noscript",
      "video",      "center", "dir",     "menu",    "multicol",   "noframes", "search",   "listing", "textarea",
      "plaintext",  "xmp",    "details", "dialog",  "summary",    "frame",    "frameset", "iframe",  "noembed",
      "template",   "select", "button",  "marquee", "applet",     "optgroup", "datalist", "map",     "area",
      "track",      "input",  "keygen",  "param",   "audio",      "source",   "title",    "html",    "head"};
  for (const char* b : kBlocks) {
    if (n == b) return true;
  }
  return false;
}

inline bool isPreTag(const std::string& n) {
  return n == "pre" || n == "listing" || n == "textarea" || n == "plaintext" || n == "xmp";
}

struct Node {
  bool isText = false;
  std::string name;
  bool block = false;
  bool pre = false;
  Node* parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;
  std::u32string text;          // crengine text-node content
  std::vector<uint32_t> rawOf;  // per text char: raw offset of the first raw char producing it
  uint32_t rawEnd = 0;          // raw offset just past the node's last raw char
};

struct RawChar {
  char32_t cp;
  uint32_t raw;  // UINT32_MAX when outside the layout's counted text
};

class Dom {
 public:
  std::unique_ptr<Node> body;
  std::u32string rawText;  // the layout's visible codepoint stream (index == visible offset)

  bool build(const std::string& xhtml) {
    XML_Parser p = XML_ParserCreate(nullptr);
    XML_SetUserData(p, this);
    XML_SetElementHandler(p, &Dom::onStart, &Dom::onEnd);
    XML_SetCharacterDataHandler(p, &Dom::onChars);
    XML_SetCommentHandler(p, [](void* u, const XML_Char*) { static_cast<Dom*>(u)->flush(); });
    XML_SetProcessingInstructionHandler(
        p, [](void* u, const XML_Char*, const XML_Char*) { static_cast<Dom*>(u)->flush(); });
    XML_SetCdataSectionHandler(
        p, [](void* u) { static_cast<Dom*>(u)->flush(); }, [](void* u) { static_cast<Dom*>(u)->flush(); });
    XML_SetDefaultHandlerExpand(p, &Dom::onDefault);
    const bool ok = XML_Parse(p, xhtml.data(), static_cast<int>(xhtml.size()), XML_TRUE) == XML_STATUS_OK;
    XML_ParserFree(p);
    if (ok && body) compact(body.get());
    return ok && body;
  }

  // Resolves an XPointer against the reference DOM: the text node and char offset.
  bool resolve(const std::string& xp, const Node*& node, uint32_t& offset) const {
    if (!body) return false;
    size_t pos = xp.find("/DocFragment");
    if (pos == std::string::npos) return false;
    pos = xp.find("/body", pos + 1);
    if (pos == std::string::npos) return false;
    pos += 5;
    if (pos < xp.size() && xp[pos] == '[') pos = xp.find(']', pos) + 1;
    const Node* cur = body.get();
    while (pos < xp.size() && xp[pos] == '/') {
      size_t end = pos + 1;
      while (end < xp.size() && xp[end] != '/' && xp[end] != '.') end++;
      std::string step = xp.substr(pos + 1, end - pos - 1);
      int index = 1;
      const size_t br = step.find('[');
      if (br != std::string::npos) {
        index = std::stoi(step.substr(br + 1));
        step = step.substr(0, br);
      }
      int seen = 0;
      const Node* next = nullptr;
      for (const auto& c : cur->children) {
        const bool match = step == "text()" ? c->isText : (!c->isText && c->name == step);
        if (match && ++seen == index) {
          next = c.get();
          break;
        }
      }
      if (!next) return false;
      cur = next;
      pos = end;
    }
    if (pos >= xp.size() || xp[pos] != '.') return false;
    offset = static_cast<uint32_t>(std::stoul(xp.substr(pos + 1)));
    node = cur;
    return true;
  }

  // Raw offset the reference maps (node, offset) to; rawEnd for an end-of-node offset.
  static uint32_t rawFor(const Node* node, const uint32_t offset) {
    if (!node->isText) return UINT32_MAX;
    if (offset < node->rawOf.size()) return node->rawOf[offset];
    return node->rawEnd;
  }

 private:
  std::vector<Node*> stack;
  std::vector<RawChar> pending;
  bool insideBody = false;
  int nonVisibleDepth = 0;
  uint32_t raw = 0;

  static void XMLCALL onStart(void* u, const XML_Char* rawName, const XML_Char**) {
    auto* self = static_cast<Dom*>(u);
    const std::string name = lowerLocal(rawName);
    if (!self->insideBody) {
      if (name != "body") return;
      self->insideBody = true;
      self->body = std::make_unique<Node>();
      self->body->name = "body";
      self->body->block = true;
      self->stack.push_back(self->body.get());
      return;
    }
    self->flush();
    if (self->nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) self->nonVisibleDepth++;
    auto child = std::make_unique<Node>();
    child->name = name;
    child->block = isBlockTag(name);
    child->pre = isPreTag(name) || self->stack.back()->pre;
    child->parent = self->stack.back();
    Node* raw = child.get();
    self->stack.back()->children.push_back(std::move(child));
    self->stack.push_back(raw);
  }

  static void XMLCALL onEnd(void* u, const XML_Char*) {
    auto* self = static_cast<Dom*>(u);
    if (!self->insideBody) return;
    self->flush();
    if (self->stack.size() == 1) {
      self->stack.clear();
      self->insideBody = false;
      return;
    }
    if (self->nonVisibleDepth > 0) self->nonVisibleDepth--;
    self->stack.pop_back();
  }

  static void XMLCALL onChars(void* u, const XML_Char* s, int len) {
    auto* self = static_cast<Dom*>(u);
    if (!self->insideBody) return;
    const bool counted = self->nonVisibleDepth == 0;
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    const auto* end = p + len;
    while (p < end) {
      const char32_t cp = utf8NextCodepoint(&p);
      self->pending.push_back({cp, counted ? self->raw : UINT32_MAX});
      if (counted) {
        self->rawText.push_back(cp);
        self->raw++;
      }
    }
  }

  static void XMLCALL onDefault(void* u, const XML_Char* s, int len) {
    if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
      const char* v = lookupHtmlEntity(s, static_cast<size_t>(len));
      if (v) {
        onChars(u, v, static_cast<int>(std::strlen(v)));
      } else {
        onChars(u, s, len);
      }
    }
  }

  static bool isSpace(char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

  void flush() {
    if (pending.empty() || stack.empty()) {
      pending.clear();
      return;
    }
    Node* parent = stack.back();
    bool allSpace = true;
    for (const auto& rc : pending) allSpace = allSpace && isSpace(rc.cp);
    if (!parent->pre && allSpace && parent->block && parent->children.empty()) {
      pending.clear();  // ldomElementWriter::onText
      return;
    }
    auto node = std::make_unique<Node>();
    node->isText = true;
    node->parent = parent;
    bool prevSpace = false;
    unsigned column = 0;
    bool first = true;
    for (const auto& rc : pending) {
      if (parent->pre) {
        if (first && rc.cp == '\n' && (parent->name == "pre" || parent->name == "textarea") &&
            parent->children.empty()) {
          first = false;
          continue;
        }
        if (rc.cp == '\t') {
          const unsigned width = 8 - (column & 7);
          for (unsigned i = 0; i < width; i++) {
            node->text.push_back(' ');
            node->rawOf.push_back(rc.raw);
          }
          column += width;
        } else {
          node->text.push_back(rc.cp);
          node->rawOf.push_back(rc.raw);
          column = rc.cp == '\n' ? 0 : column + 1;
        }
      } else if (isSpace(rc.cp)) {
        if (!prevSpace) {
          node->text.push_back(' ');
          node->rawOf.push_back(rc.raw);
        }
        prevSpace = true;
      } else {
        node->text.push_back(rc.cp);
        node->rawOf.push_back(rc.raw);
        prevSpace = false;
      }
      first = false;
    }
    node->rawEnd = pending.back().raw == UINT32_MAX ? UINT32_MAX : pending.back().raw + 1;
    pending.clear();
    parent->children.push_back(std::move(node));
  }

  static bool isWhitespaceText(const Node* n) {
    if (!n->isText) return false;
    for (char32_t c : n->text) {
      if (!isSpace(c)) return false;
    }
    return true;
  }
  static bool isInline(const Node* n) { return n->isText || !n->block; }

  static void compact(Node* e) {
    for (auto& c : e->children) {
      if (!c->isText) compact(c.get());
    }
    if (e->pre) return;
    bool hasBlock = false;
    for (const auto& c : e->children) hasBlock = hasBlock || (!c->isText && c->block);
    if (!hasBlock) return;
    std::vector<std::unique_ptr<Node>> kept;
    auto& ch = e->children;
    for (size_t i = 0; i < ch.size();) {
      if (!isWhitespaceText(ch[i].get())) {
        kept.push_back(std::move(ch[i++]));
        continue;
      }
      size_t j = i;
      while (j + 1 < ch.size() && isWhitespaceText(ch[j + 1].get())) j++;
      const Node* prev = i > 0 ? (kept.empty() ? nullptr : kept.back().get()) : nullptr;
      const Node* next = j + 1 < ch.size() ? ch[j + 1].get() : nullptr;
      const bool keep = prev && next && isInline(prev) && isInline(next);
      for (size_t k = i; k <= j; k++) {
        if (keep) kept.push_back(std::move(ch[k]));
      }
      i = j + 1;
    }
    ch = std::move(kept);
  }
};

}  // namespace credomref
