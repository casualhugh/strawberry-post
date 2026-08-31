#include "storage_core.h"

namespace StrawberryCore {
namespace {

bool validOperations(const StorageOperations& operations) {
  return operations.readExact != nullptr && operations.writeExact != nullptr &&
         operations.exists != nullptr && operations.remove != nullptr &&
         operations.rename != nullptr;
}

bool removeIfPresent(const StorageOperations& operations, const char* path) {
  return !operations.exists(operations.context, path) ||
         operations.remove(operations.context, path);
}

}  // namespace

StorageLoadSource readStorageWithFallback(
    const StorageOperations& operations, const char* primaryPath,
    const char* backupPath, void* destination, size_t size,
    StorageValidator validator, void* validatorContext) {
  if (!validOperations(operations) || primaryPath == nullptr ||
      backupPath == nullptr || destination == nullptr || size == 0 ||
      validator == nullptr) {
    return StorageLoadSource::None;
  }

  const auto readAndValidate = [&](const char* path) {
    if (!operations.readExact(operations.context, path, destination, size)) {
      return false;
    }
    return validator(destination, size, validatorContext);
  };

  if (readAndValidate(primaryPath)) {
    return StorageLoadSource::Primary;
  }
  if (readAndValidate(backupPath)) {
    return StorageLoadSource::Backup;
  }
  return StorageLoadSource::None;
}

bool prepareBackupRecovery(const StorageOperations& operations,
                           const char* rejectedPrimaryPath) {
  if (!validOperations(operations) || rejectedPrimaryPath == nullptr) {
    return false;
  }
  return removeIfPresent(operations, rejectedPrimaryPath);
}

bool writeStorageAtomic(const StorageOperations& operations,
                        const char* primaryPath, const char* temporaryPath,
                        const char* backupPath, const void* data, size_t size) {
  if (!validOperations(operations) || primaryPath == nullptr ||
      temporaryPath == nullptr || backupPath == nullptr || data == nullptr ||
      size == 0) {
    return false;
  }

  if (!removeIfPresent(operations, temporaryPath)) {
    return false;
  }
  if (!operations.writeExact(operations.context, temporaryPath, data, size)) {
    removeIfPresent(operations, temporaryPath);
    return false;
  }

  const bool hadPrimary = operations.exists(operations.context, primaryPath);
  // A stale backup only needs removal when the primary must move into its
  // place. In backup-only recovery, retain that last known-good copy until the
  // new primary has been promoted successfully.
  if (hadPrimary && !removeIfPresent(operations, backupPath)) {
    removeIfPresent(operations, temporaryPath);
    return false;
  }
  if (hadPrimary &&
      !operations.rename(operations.context, primaryPath, backupPath)) {
    removeIfPresent(operations, temporaryPath);
    return false;
  }

  if (!operations.rename(operations.context, temporaryPath, primaryPath)) {
    removeIfPresent(operations, temporaryPath);
    if (hadPrimary && operations.exists(operations.context, backupPath) &&
        !operations.exists(operations.context, primaryPath)) {
      // Best effort only: if restoration fails, validated reads can still use
      // the backup at its existing path.
      operations.rename(operations.context, backupPath, primaryPath);
    }
    return false;
  }

  // The new primary is committed at this point. A failed cleanup leaves a
  // harmless old backup which the next write will explicitly deal with.
  removeIfPresent(operations, backupPath);
  return true;
}

}  // namespace StrawberryCore
