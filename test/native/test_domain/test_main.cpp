#include <string.h>

#include <unity.h>

#include "strawberry_core.h"

void setUp() {}
void tearDown() {}

namespace {

using StrawberryCore::CompactionResult;

void test_deadline_is_exact_and_wrap_safe() {
  TEST_ASSERT_FALSE(StrawberryCore::deadlineReached(99, 100));
  TEST_ASSERT_TRUE(StrawberryCore::deadlineReached(100, 100));
  TEST_ASSERT_TRUE(StrawberryCore::deadlineReached(101, 100));

  // A deadline scheduled across rollover remains in the future until 0x20.
  TEST_ASSERT_FALSE(
      StrawberryCore::deadlineReached(UINT32_C(0xffffffff), UINT32_C(0x20)));
  TEST_ASSERT_FALSE(
      StrawberryCore::deadlineReached(UINT32_C(0x1f), UINT32_C(0x20)));
  TEST_ASSERT_TRUE(
      StrawberryCore::deadlineReached(UINT32_C(0x20), UINT32_C(0x20)));
  TEST_ASSERT_TRUE(
      StrawberryCore::deadlineReached(UINT32_C(0x21), UINT32_C(0x20)));
}

void test_utf8_accepts_unicode_scalars_and_emoji() {
  const char strawberry[] = {static_cast<char>(0xf0), static_cast<char>(0x9f),
                             static_cast<char>(0x8d), static_cast<char>(0x93)};
  const char boundaries[] = {
      static_cast<char>(0xc2), static_cast<char>(0x80),  // U+0080
      static_cast<char>(0xe0), static_cast<char>(0xa0),
      static_cast<char>(0x80),  // U+0800
      static_cast<char>(0xf0), static_cast<char>(0x90),
      static_cast<char>(0x80), static_cast<char>(0x80),  // U+10000
      static_cast<char>(0xf4), static_cast<char>(0x8f),
      static_cast<char>(0xbf), static_cast<char>(0xbf)};  // U+10FFFF

  TEST_ASSERT_TRUE(StrawberryCore::validUserText("plain text", 10, false));
  TEST_ASSERT_TRUE(StrawberryCore::validUserText(strawberry,
                                                 sizeof(strawberry), false));
  TEST_ASSERT_TRUE(StrawberryCore::validUserText(boundaries,
                                                 sizeof(boundaries), false));
  TEST_ASSERT_TRUE(StrawberryCore::validUserText(nullptr, 0, false));
}

void test_utf8_rejects_malformed_sequences_and_controls() {
  const char overlong2[] = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
  const char overlong3[] = {static_cast<char>(0xe0), static_cast<char>(0x80),
                            static_cast<char>(0xaf)};
  const char surrogate[] = {static_cast<char>(0xed), static_cast<char>(0xa0),
                            static_cast<char>(0x80)};
  const char tooHigh[] = {static_cast<char>(0xf4), static_cast<char>(0x90),
                          static_cast<char>(0x80), static_cast<char>(0x80)};
  const char truncated[] = {static_cast<char>(0xf0), static_cast<char>(0x9f),
                            static_cast<char>(0x8d)};
  const char badContinuation[] = {static_cast<char>(0xe2), 'A',
                                  static_cast<char>(0xa1)};
  const char embeddedNul[] = {'a', '\0', 'b'};

  TEST_ASSERT_FALSE(StrawberryCore::validUserText(overlong2,
                                                  sizeof(overlong2), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(overlong3,
                                                  sizeof(overlong3), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(surrogate,
                                                  sizeof(surrogate), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(tooHigh,
                                                  sizeof(tooHigh), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(truncated,
                                                  sizeof(truncated), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(
      badContinuation, sizeof(badContinuation), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText(embeddedNul,
                                                  sizeof(embeddedNul), true));
  TEST_ASSERT_FALSE(StrawberryCore::validUserText("line\nbreak", 10, false));
  TEST_ASSERT_TRUE(StrawberryCore::validUserText("line\nbreak", 10, true));
}

void test_submission_hash_is_deterministic_and_field_separated() {
  const uint32_t abc = StrawberryCore::appendSubmissionHash(0, "abc", 3);
  TEST_ASSERT_EQUAL_HEX32(UINT32_C(0x53354b1c), abc);
  TEST_ASSERT_EQUAL_HEX32(
      abc, StrawberryCore::appendSubmissionHash(0, "abc", 3));

  uint32_t first = StrawberryCore::appendSubmissionHash(0, "ab", 2);
  first = StrawberryCore::appendSubmissionHash(first, "c", 1);
  uint32_t second = StrawberryCore::appendSubmissionHash(0, "a", 1);
  second = StrawberryCore::appendSubmissionHash(second, "bc", 2);
  TEST_ASSERT_NOT_EQUAL(first, second);
}

void test_duplicate_window_is_exact_and_wrap_safe() {
  const uint32_t hash = UINT32_C(0x12345678);
  TEST_ASSERT_TRUE(
      StrawberryCore::recentlySubmitted(hash, hash, 1004, 1000, 5));
  TEST_ASSERT_FALSE(
      StrawberryCore::recentlySubmitted(hash, hash, 1005, 1000, 5));
  TEST_ASSERT_FALSE(StrawberryCore::recentlySubmitted(
      hash, UINT32_C(0x87654321), 1001, 1000, 5));
  TEST_ASSERT_FALSE(StrawberryCore::recentlySubmitted(hash, 0, 1001, 1000, 5));
  TEST_ASSERT_TRUE(StrawberryCore::recentlySubmitted(
      hash, hash, UINT32_C(1), UINT32_C(0xfffffffe), 5));
}

void test_clock_backfill_preserves_current_boot_age_and_resets_old_boots() {
  TEST_ASSERT_EQUAL_UINT64(
      UINT64_C(1700000900), StrawberryCore::backfillCreationEpochSeconds(
                                UINT64_C(1700001000), 250000, 150000, true));
  TEST_ASSERT_EQUAL_UINT64(
      UINT64_C(1700001000), StrawberryCore::backfillCreationEpochSeconds(
                                UINT64_C(1700001000), 250000, 150000, false));
  TEST_ASSERT_EQUAL_UINT64(
      UINT64_C(1700000997), StrawberryCore::backfillCreationEpochSeconds(
                                UINT64_C(1700001000), 999, UINT32_MAX - 2000,
                                true));
}

void test_record_age_requires_a_known_non_future_creation_time() {
  TEST_ASSERT_FALSE(StrawberryCore::recordAgeReached(200, 0, 100));
  TEST_ASSERT_FALSE(StrawberryCore::recordAgeReached(200, 201, 100));
  TEST_ASSERT_FALSE(StrawberryCore::recordAgeReached(200, 101, 100));
  TEST_ASSERT_TRUE(StrawberryCore::recordAgeReached(200, 100, 100));
}

struct OccupiedCodes {
  const char** codes;
  size_t count;
  bool all;
};

bool codeExists(const char* code, void* context) {
  const OccupiedCodes& occupied = *static_cast<OccupiedCodes*>(context);
  if (occupied.all) {
    return true;
  }
  for (size_t index = 0; index < occupied.count; ++index) {
    if (strcmp(code, occupied.codes[index]) == 0) {
      return true;
    }
  }
  return false;
}

void test_tracking_format_and_wraparound_allocation() {
  char code[StrawberryCore::kTrackingCodeBytes + 1] = {};
  TEST_ASSERT_TRUE(StrawberryCore::formatTrackingCode(0, code, sizeof(code)));
  TEST_ASSERT_EQUAL_STRING("STRAW-0000", code);
  TEST_ASSERT_TRUE(
      StrawberryCore::formatTrackingCode(9999, code, sizeof(code)));
  TEST_ASSERT_EQUAL_STRING("STRAW-9999", code);
  TEST_ASSERT_FALSE(
      StrawberryCore::formatTrackingCode(10000, code, sizeof(code)));

  const char* used[] = {"STRAW-9999", "STRAW-0000"};
  OccupiedCodes occupied{used, 2, false};
  uint16_t next = 77;
  TEST_ASSERT_TRUE(StrawberryCore::allocateTrackingCode(
      9999, code, sizeof(code), codeExists, &occupied, next));
  TEST_ASSERT_EQUAL_STRING("STRAW-0001", code);
  TEST_ASSERT_EQUAL_UINT16(2, next);
}

void test_tracking_exhaustion_does_not_change_cursor_result() {
  char code[StrawberryCore::kTrackingCodeBytes + 1] = "unchanged";
  OccupiedCodes occupied{nullptr, 0, true};
  uint16_t next = 4321;
  TEST_ASSERT_FALSE(StrawberryCore::allocateTrackingCode(
      15, code, sizeof(code), codeExists, &occupied, next));
  TEST_ASSERT_EQUAL_UINT16(4321, next);
  TEST_ASSERT_EQUAL_STRING("", code);
}

struct CompactRecord {
  uint8_t id;
  uint8_t remove;
};

struct CommitProbe {
  CompactRecord* records;
  uint16_t* count;
  bool succeed;
  size_t calls;
  bool sawCompactedState;
};

bool shouldRemove(const void* record, void*) {
  return static_cast<const CompactRecord*>(record)->remove != 0;
}

bool commitCompaction(void* context) {
  CommitProbe& probe = *static_cast<CommitProbe*>(context);
  ++probe.calls;
  probe.sawCompactedState = *probe.count == 2 && probe.records[0].id == 1 &&
                            probe.records[1].id == 3;
  return probe.succeed;
}

void test_compaction_commits_stable_order_once() {
  CompactRecord records[] = {{0, 1}, {1, 0}, {2, 1}, {3, 0}};
  uint16_t count = 4;
  CommitProbe probe{records, &count, true, 0, false};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CompactionResult::Committed),
      static_cast<uint8_t>(StrawberryCore::compactRecordsTransactional(
          records, count, sizeof(records[0]), shouldRemove, nullptr,
          commitCompaction, &probe)));
  TEST_ASSERT_EQUAL_UINT16(2, count);
  TEST_ASSERT_EQUAL_UINT8(1, records[0].id);
  TEST_ASSERT_EQUAL_UINT8(3, records[1].id);
  TEST_ASSERT_EQUAL_UINT(1, probe.calls);
  TEST_ASSERT_TRUE(probe.sawCompactedState);
}

void test_compaction_restores_bytes_and_count_when_commit_fails() {
  CompactRecord records[] = {{0, 1}, {1, 0}, {2, 1}, {3, 0}};
  CompactRecord original[4];
  memcpy(original, records, sizeof(records));
  uint16_t count = 4;
  CommitProbe probe{records, &count, false, 0, false};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CompactionResult::CommitFailed),
      static_cast<uint8_t>(StrawberryCore::compactRecordsTransactional(
          records, count, sizeof(records[0]), shouldRemove, nullptr,
          commitCompaction, &probe)));
  TEST_ASSERT_EQUAL_UINT16(4, count);
  TEST_ASSERT_EQUAL_MEMORY(original, records, sizeof(records));
  TEST_ASSERT_EQUAL_UINT(1, probe.calls);
  TEST_ASSERT_TRUE(probe.sawCompactedState);
}

void test_compaction_skips_persist_when_nothing_expires() {
  CompactRecord records[] = {{1, 0}, {2, 0}};
  uint16_t count = 2;
  CommitProbe probe{records, &count, true, 0, false};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(CompactionResult::NoChange),
      static_cast<uint8_t>(StrawberryCore::compactRecordsTransactional(
          records, count, sizeof(records[0]), shouldRemove, nullptr,
          commitCompaction, &probe)));
  TEST_ASSERT_EQUAL_UINT16(2, count);
  TEST_ASSERT_EQUAL_UINT(0, probe.calls);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_deadline_is_exact_and_wrap_safe);
  RUN_TEST(test_utf8_accepts_unicode_scalars_and_emoji);
  RUN_TEST(test_utf8_rejects_malformed_sequences_and_controls);
  RUN_TEST(test_submission_hash_is_deterministic_and_field_separated);
  RUN_TEST(test_duplicate_window_is_exact_and_wrap_safe);
  RUN_TEST(test_clock_backfill_preserves_current_boot_age_and_resets_old_boots);
  RUN_TEST(test_record_age_requires_a_known_non_future_creation_time);
  RUN_TEST(test_tracking_format_and_wraparound_allocation);
  RUN_TEST(test_tracking_exhaustion_does_not_change_cursor_result);
  RUN_TEST(test_compaction_commits_stable_order_once);
  RUN_TEST(test_compaction_restores_bytes_and_count_when_commit_fails);
  RUN_TEST(test_compaction_skips_persist_when_nothing_expires);
  return UNITY_END();
}
