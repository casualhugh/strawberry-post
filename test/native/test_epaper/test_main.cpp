#include <string.h>

#include <unity.h>

#include "epaper_core.h"

void setUp() {}
void tearDown() {}

namespace {

void test_sanitizes_utf8_and_spacing_for_driver_font() {
  const char input[] = {'H', 'i', ' ', static_cast<char>(0xf0),
                        static_cast<char>(0x9f), static_cast<char>(0x8d),
                        static_cast<char>(0x93), '\n', 'm', 'a', 't', 'e', 0};
  char output[32] = {};
  TEST_ASSERT_EQUAL_UINT(9,
                         EpaperCore::sanitizeAscii(input, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("Hi ? mate", output);
}

void test_sanitizer_is_bounded_and_terminated() {
  char output[5] = {'x', 'x', 'x', 'x', 'x'};
  TEST_ASSERT_EQUAL_UINT(4,
                         EpaperCore::sanitizeAscii("abcdef", output,
                                                   sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("abcd", output);
}

void test_wrap_prefers_word_boundaries_and_advances() {
  char line[16] = {};
  size_t next = 0;
  TEST_ASSERT_EQUAL_UINT(
      8, EpaperCore::wrapLine("glittery cowboy dancing", 0, 12, line,
                              sizeof(line), next));
  TEST_ASSERT_EQUAL_STRING("glittery", line);
  TEST_ASSERT_EQUAL_UINT(9, next);
  TEST_ASSERT_EQUAL_UINT(
      6, EpaperCore::wrapLine("glittery cowboy dancing", next, 12, line,
                             sizeof(line), next));
  TEST_ASSERT_EQUAL_STRING("cowboy", line);
}

void test_rotation_handles_empty_and_wraps() {
  TEST_ASSERT_EQUAL_UINT(0, EpaperCore::previousNoticeIndex(3, 0));
  TEST_ASSERT_EQUAL_UINT(2, EpaperCore::previousNoticeIndex(0, 3));
  TEST_ASSERT_EQUAL_UINT(0, EpaperCore::previousNoticeIndex(1, 3));
  TEST_ASSERT_EQUAL_UINT(0, EpaperCore::nextNoticeIndex(3, 0));
  TEST_ASSERT_EQUAL_UINT(2, EpaperCore::nextNoticeIndex(1, 3));
  TEST_ASSERT_EQUAL_UINT(0, EpaperCore::nextNoticeIndex(2, 3));
}

void test_wifi_qr_follows_three_notice_slots_and_persists_when_empty() {
  EpaperCore::RotationState frame{0, 1, false};
  frame = EpaperCore::nextAutomaticFrame(frame, 5, 3);
  TEST_ASSERT_FALSE(frame.wifiQr);
  TEST_ASSERT_EQUAL_UINT(1, frame.noticeIndex);
  TEST_ASSERT_EQUAL_UINT16(2, frame.noticeSlotsShown);
  frame = EpaperCore::nextAutomaticFrame(frame, 5, 3);
  TEST_ASSERT_FALSE(frame.wifiQr);
  TEST_ASSERT_EQUAL_UINT(2, frame.noticeIndex);
  TEST_ASSERT_EQUAL_UINT16(3, frame.noticeSlotsShown);
  frame = EpaperCore::nextAutomaticFrame(frame, 5, 3);
  TEST_ASSERT_TRUE(frame.wifiQr);
  TEST_ASSERT_EQUAL_UINT(2, frame.noticeIndex);
  frame = EpaperCore::nextAutomaticFrame(frame, 5, 3);
  TEST_ASSERT_FALSE(frame.wifiQr);
  TEST_ASSERT_EQUAL_UINT(3, frame.noticeIndex);
  TEST_ASSERT_EQUAL_UINT16(1, frame.noticeSlotsShown);

  frame = EpaperCore::nextAutomaticFrame(frame, 0, 3);
  TEST_ASSERT_TRUE(frame.wifiQr);
  TEST_ASSERT_EQUAL_UINT(0, frame.noticeIndex);
  frame = EpaperCore::nextAutomaticFrame(frame, 0, 3);
  TEST_ASSERT_TRUE(frame.wifiQr);
}

void test_zero_qr_cadence_disables_only_periodic_frames() {
  const EpaperCore::RotationState notice =
      EpaperCore::nextAutomaticFrame({1, UINT16_MAX, false}, 3, 0);
  TEST_ASSERT_FALSE(notice.wifiQr);
  TEST_ASSERT_EQUAL_UINT(2, notice.noticeIndex);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, notice.noticeSlotsShown);
  TEST_ASSERT_TRUE(EpaperCore::nextAutomaticFrame(notice, 0, 0).wifiQr);
}

void test_button_debounce_emits_only_a_stable_press_edge() {
  EpaperCore::DebouncedButton button{};
  EpaperCore::initializeButton(button, false, 100);

  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 110, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, false, 120, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 125, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 154, 30));
  TEST_ASSERT_TRUE(EpaperCore::buttonPressed(button, true, 155, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 200, 30));

  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, false, 210, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, false, 240, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 250, 30));
  TEST_ASSERT_TRUE(EpaperCore::buttonPressed(button, true, 280, 30));
  TEST_ASSERT_FALSE(EpaperCore::buttonPressed(button, true, 320, 30));
}

void test_button_debounce_handles_millis_wraparound() {
  EpaperCore::DebouncedButton button{};
  EpaperCore::initializeButton(button, false, UINT32_MAX - 20);
  TEST_ASSERT_FALSE(
      EpaperCore::buttonPressed(button, true, UINT32_MAX - 10, 30));
  TEST_ASSERT_TRUE(EpaperCore::buttonPressed(button, true, 19, 30));
}

void test_refresh_policy_skips_identical_frames_and_periodically_cleans() {
  using EpaperCore::RefreshKind;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RefreshKind::Full),
                          static_cast<uint8_t>(EpaperCore::chooseRefresh(
                              false, 0, 1, 0, 10)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RefreshKind::None),
                          static_cast<uint8_t>(EpaperCore::chooseRefresh(
                              true, 1, 1, 4, 10)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RefreshKind::Fast),
                          static_cast<uint8_t>(EpaperCore::chooseRefresh(
                              true, 1, 2, 9, 10)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RefreshKind::Full),
                          static_cast<uint8_t>(EpaperCore::chooseRefresh(
                              true, 1, 2, 10, 10)));
}

void test_snapshot_hash_changes_with_displayed_content() {
  EpaperCore::Snapshot first{};
  strcpy(first.category, "General");
  strcpy(first.message, "Meet at the river");
  EpaperCore::Snapshot second = first;
  TEST_ASSERT_EQUAL_HEX32(EpaperCore::snapshotHash(first),
                          EpaperCore::snapshotHash(second));
  ++second.delivered;
  TEST_ASSERT_NOT_EQUAL(EpaperCore::snapshotHash(first),
                        EpaperCore::snapshotHash(second));
  second = first;
  second.wifiQr = 1;
  TEST_ASSERT_NOT_EQUAL(EpaperCore::snapshotHash(first),
                        EpaperCore::snapshotHash(second));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sanitizes_utf8_and_spacing_for_driver_font);
  RUN_TEST(test_sanitizer_is_bounded_and_terminated);
  RUN_TEST(test_wrap_prefers_word_boundaries_and_advances);
  RUN_TEST(test_rotation_handles_empty_and_wraps);
  RUN_TEST(test_wifi_qr_follows_three_notice_slots_and_persists_when_empty);
  RUN_TEST(test_zero_qr_cadence_disables_only_periodic_frames);
  RUN_TEST(test_button_debounce_emits_only_a_stable_press_edge);
  RUN_TEST(test_button_debounce_handles_millis_wraparound);
  RUN_TEST(test_refresh_policy_skips_identical_frames_and_periodically_cleans);
  RUN_TEST(test_snapshot_hash_changes_with_displayed_content);
  return UNITY_END();
}
