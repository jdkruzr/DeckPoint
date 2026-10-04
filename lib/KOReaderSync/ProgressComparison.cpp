#include "ProgressComparison.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float SAME_PROGRESS_EPSILON = 0.001f;

ProgressComparison compareOrdered(const uint32_t local, const uint32_t remote) {
  if (local > remote) return ProgressComparison::LocalAhead;
  if (local < remote) return ProgressComparison::RemoteAhead;
  return ProgressComparison::Synchronized;
}
}  // namespace

ProgressComparison compareProgress(const CrossPointPosition& local, const float localPercentage,
                                   const CrossPointPosition& remote, const float remotePercentage) {
  if (local.hasResolvedSpineIndex && remote.hasResolvedSpineIndex) {
    if (local.spineIndex != remote.spineIndex) {
      return local.spineIndex > remote.spineIndex ? ProgressComparison::LocalAhead : ProgressComparison::RemoteAhead;
    }

    if (local.hasMappedPage && remote.hasMappedPage && local.pageNumber == remote.pageNumber) {
      return ProgressComparison::Synchronized;
    }

    if (local.hasVisibleTextOffset && remote.hasVisibleTextOffset) {
      return compareOrdered(local.visibleTextOffset, remote.visibleTextOffset);
    }

    if (local.hasMappedPage && remote.hasMappedPage) {
      if (local.pageNumber > remote.pageNumber) return ProgressComparison::LocalAhead;
      if (local.pageNumber < remote.pageNumber) return ProgressComparison::RemoteAhead;
      return ProgressComparison::Synchronized;
    }
  }

  if (std::isfinite(localPercentage) && std::isfinite(remotePercentage)) {
    const float delta = localPercentage - remotePercentage;
    if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) return ProgressComparison::Synchronized;
    return delta > 0.0f ? ProgressComparison::LocalAhead : ProgressComparison::RemoteAhead;
  }

  return ProgressComparison::Unknown;
}

RemoteRecordChoice selectRemoteRecord(const CrossPointPosition& primary, const float primaryPercentage,
                                      const CrossPointPosition& alternate, const float alternatePercentage) {
  return compareProgress(primary, primaryPercentage, alternate, alternatePercentage) == ProgressComparison::RemoteAhead
             ? RemoteRecordChoice::Alternate
             : RemoteRecordChoice::Primary;
}

float koreaderPercentage(const float chapterStart, const float chapterEnd, const int page, const int totalPages) {
  double within = totalPages > 0 ? static_cast<double>(page + 1) / static_cast<double>(totalPages) : 0.0;
  within = std::min(1.0, std::max(0.0, within));
  double pct = chapterStart + (static_cast<double>(chapterEnd) - chapterStart) * within;
  pct = std::min(1.0, std::max(0.0, pct));
  // Math.roundPercent; the epsilon keeps 0.7582 from flooring to 0.7581.
  return static_cast<float>(std::floor(pct * 10000.0 + 1e-6) / 10000.0);
}
