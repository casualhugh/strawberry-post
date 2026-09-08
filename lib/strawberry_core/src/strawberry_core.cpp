#include "strawberry_core.h"

#include <string.h>

namespace StrawberryCore {
namespace {

constexpr uint32_t kHalfUint32Range = UINT32_C(0x80000000);
constexpr uint32_t kFnv1aOffsetBasis = UINT32_C(2166136261);
constexpr uint32_t kFnv1aPrime = UINT32_C(16777619);
// A byte that cannot occur in valid UTF-8 separates adjacent form fields.
constexpr uint8_t kSubmissionFieldSeparator = 0xff;
constexpr uint16_t kMaxTransactionalRecords = 64;

void swapRecords(uint8_t* records, size_t firstIndex, size_t secondIndex,
                 size_t recordSize) {
  uint8_t* first = records + firstIndex * recordSize;
  uint8_t* second = records + secondIndex * recordSize;
  for (size_t offset = 0; offset < recordSize; ++offset) {
    const uint8_t temporary = first[offset];
    first[offset] = second[offset];
    second[offset] = temporary;
  }
}

}  // namespace

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<uint32_t>(nowMs - deadlineMs) < kHalfUint32Range;
}

bool validUserText(const char* bytes, size_t length, bool allowNewlines) {
  if (bytes == nullptr && length != 0) {
    return false;
  }

  const uint8_t* input = reinterpret_cast<const uint8_t*>(bytes);
  size_t index = 0;
  while (index < length) {
    const uint8_t first = input[index];
    if (first < 0x80) {
      const bool allowedWhitespace =
          allowNewlines &&
          (first == static_cast<uint8_t>('\n') ||
           first == static_cast<uint8_t>('\r') ||
           first == static_cast<uint8_t>('\t'));
      if ((first < 0x20 || first == 0x7f) && !allowedWhitespace) {
        return false;
      }
      ++index;
      continue;
    }

    size_t continuationCount = 0;
    uint32_t codePoint = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      continuationCount = 1;
      codePoint = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuationCount = 2;
      codePoint = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuationCount = 3;
      codePoint = first & 0x07;
    } else {
      return false;
    }

    if (index + continuationCount >= length) {
      return false;
    }
    for (size_t offset = 1; offset <= continuationCount; ++offset) {
      const uint8_t continuation = input[index + offset];
      if ((continuation & 0xc0) != 0x80) {
        return false;
      }
      codePoint = (codePoint << 6) | (continuation & 0x3f);
    }

    const bool overlong =
        (continuationCount == 2 && codePoint < 0x800) ||
        (continuationCount == 3 && codePoint < 0x10000);
    const bool surrogate = codePoint >= 0xd800 && codePoint <= 0xdfff;
    if (overlong || surrogate || codePoint > 0x10ffff) {
      return false;
    }
    index += continuationCount + 1;
  }
  return true;
}

uint32_t appendSubmissionHash(uint32_t hash, const char* bytes, size_t length) {
  if (hash == 0) {
    hash = kFnv1aOffsetBasis;
  }
  if (bytes == nullptr) {
    length = 0;
  }
  for (size_t index = 0; index < length; ++index) {
    hash ^= static_cast<uint8_t>(bytes[index]);
    hash *= kFnv1aPrime;
  }
  hash ^= kSubmissionFieldSeparator;
  hash *= kFnv1aPrime;
  return hash;
}

bool recentlySubmitted(uint32_t hash, uint32_t previousHash, uint32_t nowMs,
                       uint32_t previousTimeMs, uint32_t windowMs) {
  return previousHash != 0 && hash == previousHash &&
         static_cast<uint32_t>(nowMs - previousTimeMs) < windowMs;
}

uint64_t backfillCreationEpochSeconds(uint64_t currentEpochSeconds,
                                      uint32_t currentUptimeMs,
                                      uint32_t creationUptimeMs,
                                      bool createdThisBoot) {
  if (!createdThisBoot) return currentEpochSeconds;
  const uint64_t ageSeconds =
      static_cast<uint32_t>(currentUptimeMs - creationUptimeMs) / 1000U;
  return ageSeconds > currentEpochSeconds ? 0
                                          : currentEpochSeconds - ageSeconds;
}

bool recordAgeReached(uint64_t currentEpochSeconds,
                      uint64_t creationEpochSeconds,
                      uint64_t lifetimeSeconds) {
  return creationEpochSeconds != 0 &&
         currentEpochSeconds >= creationEpochSeconds &&
         currentEpochSeconds - creationEpochSeconds >= lifetimeSeconds;
}

bool formatTrackingCode(uint16_t number, char* destination,
                        size_t destinationSize) {
  if (destination == nullptr || destinationSize < kTrackingCodeBytes + 1 ||
      number >= kTrackingNumberLimit) {
    return false;
  }
  memcpy(destination, kTrackingPrefix, kTrackingPrefixBytes);
  uint16_t divisor = kTrackingNumberLimit / 10;
  for (size_t digit = 0; digit < kTrackingDigitCount; ++digit) {
    destination[kTrackingPrefixBytes + digit] =
        static_cast<char>('0' + (number / divisor) % 10);
    divisor /= 10;
  }
  destination[kTrackingCodeBytes] = '\0';
  return true;
}

bool allocateTrackingCode(uint16_t firstNumber, char* destination,
                          size_t destinationSize, TrackingExists exists,
                          void* context, uint16_t& nextNumber) {
  if (destination == nullptr || exists == nullptr ||
      destinationSize < kTrackingCodeBytes + 1) {
    return false;
  }
  uint16_t candidate = firstNumber % kTrackingNumberLimit;
  for (uint16_t attempt = 0; attempt < kTrackingNumberLimit; ++attempt) {
    if (!formatTrackingCode(candidate, destination, destinationSize)) {
      return false;
    }
    if (!exists(destination, context)) {
      nextNumber =
          static_cast<uint16_t>((candidate + 1) % kTrackingNumberLimit);
      return true;
    }
    candidate =
        static_cast<uint16_t>((candidate + 1) % kTrackingNumberLimit);
  }
  destination[0] = '\0';
  return false;
}

CompactionResult compactRecordsTransactional(
    void* records, uint16_t& count, size_t recordSize,
    RecordPredicate shouldRemove, void* predicateContext,
    CompactionCommit commit, void* commitContext) {
  if ((records == nullptr && count != 0) || recordSize == 0 ||
      shouldRemove == nullptr || commit == nullptr ||
      count > kMaxTransactionalRecords) {
    return CompactionResult::InvalidArguments;
  }

  uint8_t* recordBytes = static_cast<uint8_t*>(records);
  const uint16_t originalCount = count;
  uint16_t retainedCount = 0;
  uint64_t retainedOriginalPositions = 0;

  for (uint16_t originalIndex = 0; originalIndex < originalCount;
       ++originalIndex) {
    const void* record = recordBytes + originalIndex * recordSize;
    if (shouldRemove(record, predicateContext)) {
      continue;
    }
    retainedOriginalPositions |= UINT64_C(1) << originalIndex;
    for (uint16_t position = originalIndex; position > retainedCount;
         --position) {
      swapRecords(recordBytes, position, position - 1, recordSize);
    }
    ++retainedCount;
  }

  if (retainedCount == originalCount) {
    return CompactionResult::NoChange;
  }

  count = retainedCount;
  if (commit(commitContext)) {
    return CompactionResult::Committed;
  }

  // Reverse the stable-partition rotations to make persistence failure fully
  // transactional from the caller's perspective.
  uint16_t retainedPosition = retainedCount;
  for (uint16_t originalIndex = originalCount; originalIndex > 0;
       --originalIndex) {
    const uint16_t zeroBasedIndex = originalIndex - 1;
    if ((retainedOriginalPositions & (UINT64_C(1) << zeroBasedIndex)) == 0) {
      continue;
    }
    --retainedPosition;
    for (uint16_t position = retainedPosition; position < zeroBasedIndex;
         ++position) {
      swapRecords(recordBytes, position, position + 1, recordSize);
    }
  }
  count = originalCount;
  return CompactionResult::CommitFailed;
}

}  // namespace StrawberryCore
