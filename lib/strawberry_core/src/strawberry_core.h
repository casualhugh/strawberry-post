#pragma once

#include <stddef.h>
#include <stdint.h>

namespace StrawberryCore {

// Wrap-safe while deadlines are scheduled less than 2^31 milliseconds away.
// Strawberry Post's 48-hour lifetime is well inside that half-range.
bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs);

bool validUserText(const char* bytes, size_t length, bool allowNewlines);

uint32_t appendSubmissionHash(uint32_t hash, const char* bytes, size_t length);

bool recentlySubmitted(uint32_t hash, uint32_t previousHash, uint32_t nowMs,
                       uint32_t previousTimeMs, uint32_t windowMs);

constexpr char kTrackingPrefix[] = "STRAW-";
constexpr size_t kTrackingPrefixBytes = sizeof(kTrackingPrefix) - 1;
constexpr size_t kTrackingDigitCount = 4;
// Four decimal digits provide the intentionally human-friendly 0000-9999
// namespace; this is a product choice, not a filesystem requirement.
constexpr uint16_t kTrackingNumberLimit = 10000;
constexpr size_t kTrackingCodeBytes =
    kTrackingPrefixBytes + kTrackingDigitCount;

bool formatTrackingCode(uint16_t number, char* destination,
                        size_t destinationSize);

using TrackingExists = bool (*)(const char* code, void* context);

// Searches from firstNumber without mutating it. nextNumber receives the
// cursor to persist only when an available code is found, making rollback
// explicit at the firmware boundary.
bool allocateTrackingCode(uint16_t firstNumber, char* destination,
                          size_t destinationSize, TrackingExists exists,
                          void* context, uint16_t& nextNumber);

enum class CompactionResult : uint8_t {
  NoChange,
  Committed,
  CommitFailed,
  InvalidArguments,
};

using RecordPredicate = bool (*)(const void* record, void* context);
using CompactionCommit = bool (*)(void* context);

// Stable-compacts records selected by shouldRemove, then calls commit exactly
// once. A failed commit restores both the original record order and count.
// The 64-record bound keeps rollback metadata in one uint64_t; all current
// Strawberry Post stores are deliberately capped at 32 records.
CompactionResult compactRecordsTransactional(
    void* records, uint16_t& count, size_t recordSize,
    RecordPredicate shouldRemove, void* predicateContext,
    CompactionCommit commit, void* commitContext);

}  // namespace StrawberryCore
