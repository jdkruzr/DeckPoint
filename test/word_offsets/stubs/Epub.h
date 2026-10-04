#pragma once

// DECKPOINT: one in-memory Epub serving both the layout parser (image probes, which
// find nothing here) and ChapterXPathResolver (spine chapter streaming).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Epub {
 public:
  struct SpineItem {
    std::string href;
  };

  Epub() = default;
  explicit Epub(std::vector<std::string> spineContents) : spineContents(std::move(spineContents)) {}

  int getSpineItemsCount() const { return static_cast<int>(spineContents.size()); }

  SpineItem getSpineItem(const int spineIndex) const {
    if (spineIndex < 0 || spineIndex >= getSpineItemsCount()) return {};
    return {"chapter" + std::to_string(spineIndex) + ".xhtml"};
  }

  template <typename Output>
  bool readItemContentsToStream(const std::string& itemHref, Output& out, const size_t chunkSize, bool = false) const {
    for (int i = 0; i < getSpineItemsCount(); i++) {
      if (itemHref != getSpineItem(i).href) continue;
      const auto& contents = spineContents[i];
      for (size_t offset = 0; offset < contents.size(); offset += chunkSize) {
        const size_t size = std::min(chunkSize, contents.size() - offset);
        out.write(reinterpret_cast<const uint8_t*>(contents.data() + offset), size);
      }
      return true;
    }
    return false;
  }

 private:
  std::vector<std::string> spineContents;
};
