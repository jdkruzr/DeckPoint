// DECKPOINT: link stubs for the word-offset tests. Unlike the parser suite, entities
// use the real table (so layout and XPointer resolution see identical text) and the
// hyphenator offers a break after every third letter so hyphen splits are exercised.
#include <Epub/Page.h>
#include <Epub/TokenBoundary.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <Utf8.h>

bool isExplicitHyphen(uint32_t) { return false; }
bool isSoftHyphen(uint32_t) { return false; }

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, bool) {
  std::vector<BreakInfo> breaks;
  const auto* begin = reinterpret_cast<const unsigned char*>(word.c_str());
  const auto* p = begin;
  size_t letters = 0;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    const bool letter = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    if (!letter) return {};
    letters++;
    if (letters % 3 == 0 && *p) breaks.push_back({static_cast<size_t>(p - begin), true});
  }
  return letters >= 6 ? breaks : std::vector<BreakInfo>{};
}

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}

void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::needsDecode() const { return false; }
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }
