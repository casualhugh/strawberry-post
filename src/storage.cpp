#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_partition.h>

#include "storage_core.h"
#include "storage_format.h"

namespace {

constexpr char kStatePath[] = "/system.dat";
constexpr uint32_t kStateMagic = makeStorageMagic('S', 'T', 'P', 'S');
constexpr uint16_t kStateVersion = 1;
constexpr size_t kStoragePathBufferSize = 48;
// An erased flash partition reads as 0xff. Sampling its beginning
// is only a conservative first-use check; non-blank mount failures are never
// auto-formatted because they may contain recoverable data.
constexpr uint8_t kErasedFlashByte = 0xff;
constexpr size_t kBlankPartitionProbeBytes = 64;

struct PersistentState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;  // Reserved schema space; always zero in version 1.
  uint32_t bootCount;
};

bool mounted = false;
uint32_t bootCount = 0;

bool littleFsReadExact(void*, const char* path, void* destination,
                       size_t size) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != size) {
    if (file) {
      file.close();
    }
    return false;
  }
  const size_t bytesRead =
      file.read(static_cast<uint8_t*>(destination), size);
  file.close();
  return bytesRead == size;
}

bool littleFsWriteExact(void*, const char* path, const void* data,
                        size_t size) {
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t bytesWritten =
      file.write(static_cast<const uint8_t*>(data), size);
  file.flush();
  file.close();
  return bytesWritten == size;
}

bool littleFsExists(void*, const char* path) {
  return LittleFS.exists(path);
}

bool littleFsRemove(void*, const char* path) {
  return LittleFS.remove(path);
}

bool littleFsRename(void*, const char* from, const char* to) {
  return LittleFS.rename(from, to);
}

const StrawberryCore::StorageOperations kLittleFsOperations{
    nullptr, littleFsReadExact, littleFsWriteExact, littleFsExists,
    littleFsRemove, littleFsRename};

bool validPersistentState(const void* data, size_t size, void*) {
  if (size != sizeof(PersistentState)) {
    return false;
  }
  const PersistentState& state =
      *static_cast<const PersistentState*>(data);
  return state.magic == kStateMagic && state.version == kStateVersion;
}

bool filesystemPartitionLooksBlank() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (partition == nullptr) {
    return false;
  }
  uint8_t sample[kBlankPartitionProbeBytes];
  if (esp_partition_read(partition, 0, sample, sizeof(sample)) != ESP_OK) {
    return false;
  }
  for (uint8_t value : sample) {
    if (value != kErasedFlashByte) {
      return false;
    }
  }
  return true;
}

bool makeSiblingPath(const char* path, const char* suffix, char* destination,
                     size_t destinationSize) {
  const int written =
      snprintf(destination, destinationSize, "%s%s", path, suffix);
  return written > 0 && static_cast<size_t>(written) < destinationSize;
}

bool mountFilesystem() {
  if (LittleFS.begin(false)) {
    return true;
  }

  if (!filesystemPartitionLooksBlank()) {
    Serial.println("LittleFS mount failed on non-blank storage; refusing to format.");
    return false;
  }
  Serial.println("Blank LittleFS partition detected; formatting for first use.");
  if (!LittleFS.format()) {
    Serial.println("LittleFS format failed.");
    return false;
  }
  return LittleFS.begin(false);
}

}  // namespace

bool readStorageFileValidated(const char* path, void* destination, size_t size,
                              StorageFileValidator validator,
                              void* validatorContext) {
  if (!mounted || path == nullptr || destination == nullptr || size == 0 ||
      validator == nullptr) {
    return false;
  }

  char backupPath[kStoragePathBufferSize];
  if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return false;
  }
  const StrawberryCore::StorageLoadSource source =
      StrawberryCore::readStorageWithFallback(
          kLittleFsOperations, path, backupPath, destination, size, validator,
          validatorContext);
  if (source == StrawberryCore::StorageLoadSource::None) {
    return false;
  }
  if (source == StrawberryCore::StorageLoadSource::Backup &&
      !StrawberryCore::prepareBackupRecovery(kLittleFsOperations, path)) {
    Serial.printf(
        "Could not remove rejected storage primary %s; disabling writes.\n",
        path);
    mounted = false;
    return false;
  }
  return true;
}

bool writeStorageFileAtomic(const char* path, const void* data, size_t size) {
  if (!mounted || path == nullptr || data == nullptr || size == 0) {
    return false;
  }

  char temporaryPath[kStoragePathBufferSize];
  char backupPath[kStoragePathBufferSize];
  if (!makeSiblingPath(path, ".tmp", temporaryPath, sizeof(temporaryPath)) ||
      !makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return false;
  }

  return StrawberryCore::writeStorageAtomic(
      kLittleFsOperations, path, temporaryPath, backupPath, data, size);
}

bool startStorage() {
  mounted = mountFilesystem();
  if (!mounted) {
    Serial.println("Persistent storage unavailable; continuing without it.");
    return false;
  }

  PersistentState state{kStateMagic, kStateVersion, 0, 0};
  PersistentState loaded{};
  if (readStorageFileValidated(kStatePath, &loaded, sizeof(loaded),
                               validPersistentState)) {
    state = loaded;
  } else {
    Serial.println("Creating persistent system state.");
  }

  ++state.bootCount;
  bootCount = state.bootCount;
  if (!writeStorageFileAtomic(kStatePath, &state, sizeof(state))) {
    Serial.println("Failed to persist system state.");
    mounted = false;
    return false;
  }

  Serial.printf("LittleFS ready: %u / %u bytes used. Persisted boot count: %lu\n",
                static_cast<unsigned>(LittleFS.usedBytes()),
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned long>(bootCount));
  return true;
}

bool storageAvailable() {
  return mounted;
}

uint32_t persistedBootCount() {
  return bootCount;
}

size_t storageUsedBytes() {
  return mounted ? LittleFS.usedBytes() : 0;
}

size_t storageTotalBytes() {
  return mounted ? LittleFS.totalBytes() : 0;
}
