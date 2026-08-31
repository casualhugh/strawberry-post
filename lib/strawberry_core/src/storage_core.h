#pragma once

#include <stddef.h>
#include <stdint.h>

namespace StrawberryCore {

struct StorageOperations {
  void* context;
  bool (*readExact)(void* context, const char* path, void* destination,
                    size_t size);
  bool (*writeExact)(void* context, const char* path, const void* data,
                     size_t size);
  bool (*exists)(void* context, const char* path);
  bool (*remove)(void* context, const char* path);
  bool (*rename)(void* context, const char* from, const char* to);
};

using StorageValidator = bool (*)(const void* data, size_t size,
                                  void* context);

enum class StorageLoadSource : uint8_t {
  None,
  Primary,
  Backup,
};

// Reads the primary only if it is both exact-sized and semantically valid;
// otherwise it gives the backup the same validation opportunity.
StorageLoadSource readStorageWithFallback(
    const StorageOperations& operations, const char* primaryPath,
    const char* backupPath, void* destination, size_t size,
    StorageValidator validator, void* validatorContext = nullptr);

// Call after a validated load reports Backup. Removing the rejected primary
// makes the next atomic write start from backup-only recovery, so it cannot
// replace the last known-good backup with corrupt primary bytes.
bool prepareBackupRecovery(const StorageOperations& operations,
                           const char* rejectedPrimaryPath);

// Replaces primary via a fully written temporary and preserves the previous
// primary as backup until the new primary rename succeeds. Callback return
// values model filesystem failures explicitly and require no heap allocation.
bool writeStorageAtomic(const StorageOperations& operations,
                        const char* primaryPath, const char* temporaryPath,
                        const char* backupPath, const void* data, size_t size);

}  // namespace StrawberryCore
