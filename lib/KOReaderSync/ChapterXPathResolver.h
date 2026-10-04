#pragma once

#include <Epub.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// DECKPOINT: visible-offset <-> XPointer mapping follows crengine's DOM text model
// (see ChapterXPathResolver.cpp) so positions round-trip with KOReader annotations.
namespace KOReaderXPointer {

// How element/text-node indexes are spelled. crengine's ldomXPointer::toStringV2()
// changed at DOM version 20260812 (crengine/src/lvtinydom.cpp header notes).
enum class Style : uint8_t {
  // "/body/DocFragment[N]/body/div[1]/p[2]/text()[1].5": the historical CrossPoint
  // KOSync form; every step indexed except the two body steps. KOReader resolves it.
  Compat,
  // DOM < 20260812: "[n]" only when the parent has more than one element of that name,
  // "text()[n]" only when it has more than one text node.
  Legacy,
  // DOM >= 20260812: every step carries an explicit index, including "/body[1]".
  Explicit,
};

// Which side of a position an offset names.
enum class Bias : uint8_t {
  // The character at the offset (a word or range start). Offsets on whitespace that
  // crengine drops move forward to the next kept character.
  Start,
  // The position just after the character before the offset (a range end).
  End,
};

}  // namespace KOReaderXPointer

class ChapterXPathResolver {
 public:
  /**
   * Resolve the Nth paragraph in a spine item to its real XHTML ancestry path.
   *
   * Returns a KOReader-compatible path like:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]
   *
   * An empty string means parsing failed or the paragraph index was not found.
   */
  static std::string findXPathForParagraph(const std::shared_ptr<Epub>& epub, int spineIndex, uint16_t paragraphIndex);

  /**
   * Resolve a zero-based visible-codepoint offset (the layout's count of every
   * character-data codepoint inside <body>, see ChapterHtmlSlimParser::characterData)
   * to the KOReader XPointer of the same character:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]/text()[1].0
   *
   * The text()[k] index and the character offset follow crengine's DOM, where
   * inter-element whitespace may be dropped and whitespace runs are collapsed.
   * An empty string means parsing failed or the offset is past the chapter's text.
   */
  static std::string findXPathForVisibleTextOffset(const std::shared_ptr<Epub>& epub, int spineIndex,
                                                   uint32_t visibleTextOffset,
                                                   KOReaderXPointer::Bias bias = KOReaderXPointer::Bias::Start,
                                                   KOReaderXPointer::Style style = KOReaderXPointer::Style::Compat);

  /**
   * Inverse of findXPathForVisibleTextOffset: resolve a KOReader XPointer (any
   * Style; element points such as ".../img[1].0" resolve to the element start)
   * to the layout's visible-codepoint offset. With relaxFirstStep the first path
   * step may match at any depth below <body> (producers that omit a wrapper).
   */
  static std::optional<uint32_t> findVisibleTextOffsetForXPath(const std::shared_ptr<Epub>& epub, int spineIndex,
                                                               const std::string& xpath, bool relaxFirstStep = false);

  /**
   * Resolve intra-spine progress to a real XHTML ancestry path plus text offset.
   *
   * Returns a KOReader-compatible path like:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]/text().96
   *
   * An empty string means parsing failed or the location could not be resolved.
   */
  static std::string findXPathForProgress(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);
};
