#include <gtest/gtest.h>

#include "AnnotationSyncPlan.h"

using annotationsync::afterPut;
using annotationsync::formatSummary;
using annotationsync::PutNext;
using annotationsync::PutState;
using annotationsync::Remote;
using annotationsync::remoteAfterGet;
using annotationsync::Summary;
using annotationsync::SummaryText;

TEST(AnnotationSyncPlan, GetOutcomes) {
  EXPECT_EQ(remoteAfterGet(dav::Error::None), Remote::Present);
  EXPECT_EQ(remoteAfterGet(dav::Error::NotFound), Remote::Absent);
  EXPECT_EQ(remoteAfterGet(dav::Error::Conflict), Remote::FolderMissing);
  for (const auto e : {dav::Error::Auth, dav::Error::Network, dav::Error::Server, dav::Error::TooLarge,
                       dav::Error::SdIo, dav::Error::NoMemory, dav::Error::Unexpected, dav::Error::Tls}) {
    EXPECT_EQ(remoteAfterGet(e), Remote::Failed) << static_cast<int>(e);
  }
}

TEST(AnnotationSyncPlan, PutSucceeds) {
  PutState s;
  EXPECT_EQ(afterPut(dav::Error::None, s), PutNext::Done);
}

TEST(AnnotationSyncPlan, PreconditionFailedRestartsOnceThenGivesUp) {
  PutState s;
  EXPECT_EQ(afterPut(dav::Error::PreconditionFailed, s), PutNext::Restart);
  EXPECT_EQ(s.attempt, 2);
  EXPECT_EQ(afterPut(dav::Error::PreconditionFailed, s), PutNext::GiveUpChanged);
  EXPECT_EQ(s.attempt, 2);
}

TEST(AnnotationSyncPlan, RestartThenSuccess) {
  PutState s;
  EXPECT_EQ(afterPut(dav::Error::PreconditionFailed, s), PutNext::Restart);
  EXPECT_EQ(afterPut(dav::Error::None, s), PutNext::Done);
}

TEST(AnnotationSyncPlan, MissingFolderCreatedOnce) {
  PutState s;
  EXPECT_EQ(afterPut(dav::Error::Conflict, s), PutNext::CreateFolderRetry);
  EXPECT_TRUE(s.folderCreated);
  EXPECT_EQ(afterPut(dav::Error::Conflict, s), PutNext::Fail);

  PutState t;
  EXPECT_EQ(afterPut(dav::Error::NotFound, t), PutNext::CreateFolderRetry);
  EXPECT_EQ(afterPut(dav::Error::NotFound, t), PutNext::Fail);
}

TEST(AnnotationSyncPlan, FolderCreatedThenPreconditionStillRetries) {
  PutState s;
  EXPECT_EQ(afterPut(dav::Error::Conflict, s), PutNext::CreateFolderRetry);
  EXPECT_EQ(afterPut(dav::Error::PreconditionFailed, s), PutNext::Restart);
  EXPECT_EQ(afterPut(dav::Error::Conflict, s), PutNext::Fail);  // folder already tried
}

TEST(AnnotationSyncPlan, OtherPutErrorsFail) {
  for (const auto e : {dav::Error::Auth, dav::Error::Network, dav::Error::Server, dav::Error::TooLarge,
                       dav::Error::NoMemory, dav::Error::Unexpected}) {
    PutState s;
    EXPECT_EQ(afterPut(e, s), PutNext::Fail) << static_cast<int>(e);
  }
}

namespace {
constexpr SummaryText TEXT = {"Highlights up to date", "Highlights uploaded", "Highlights: %s",
                              "Highlights: %s, uploaded"};

std::string summary(unsigned pulled, unsigned removed, bool uploaded, size_t size = 64) {
  Summary s;
  s.pulled = pulled;
  s.removed = removed;
  s.uploaded = uploaded;
  char out[64];
  formatSummary(s, TEXT, out, size);
  return out;
}
}  // namespace

TEST(AnnotationSyncPlan, SummaryText) {
  EXPECT_EQ(summary(0, 0, false), "Highlights up to date");
  EXPECT_EQ(summary(0, 0, true), "Highlights uploaded");
  EXPECT_EQ(summary(3, 0, false), "Highlights: +3");
  EXPECT_EQ(summary(0, 1, false), "Highlights: -1");
  EXPECT_EQ(summary(3, 1, false), "Highlights: +3 -1");
  EXPECT_EQ(summary(3, 1, true), "Highlights: +3 -1, uploaded");
  EXPECT_EQ(summary(12, 0, true), "Highlights: +12, uploaded");
}

TEST(AnnotationSyncPlan, SummaryTruncatesSafely) {
  EXPECT_EQ(summary(3, 1, true, 8), "Highlig");
  Summary s;
  char out[1] = {'x'};
  formatSummary(s, TEXT, out, 0);
  EXPECT_EQ(out[0], 'x');
}
