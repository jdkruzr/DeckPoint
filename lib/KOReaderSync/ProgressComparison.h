#pragma once

#include <cstdint>

#include "CrossPointPosition.h"

enum class ProgressComparison : uint8_t { LocalAhead, Synchronized, RemoteAhead, Unknown };

enum class RemoteRecordChoice : uint8_t { Primary, Alternate };

ProgressComparison compareProgress(const CrossPointPosition& local, float localPercentage,
                                   const CrossPointPosition& remote, float remotePercentage);

RemoteRecordChoice selectRemoteRecord(const CrossPointPosition& primary, float primaryPercentage,
                                      const CrossPointPosition& alternate, float alternatePercentage);

// DECKPOINT: KOReader's EPUB `percentage` semantics (ReaderRolling:getLastPercent
// in page mode, then KOSync's Math.roundPercent): current_page / page_count with
// a 1-based page, i.e. the book share up to the END of the current page,
// floored to 4 decimals. chapterStart / chapterEnd are the book fractions where
// the chapter begins and ends; page is 0-based within it.
float koreaderPercentage(float chapterStart, float chapterEnd, int page, int totalPages);
