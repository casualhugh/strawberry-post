#include "storage.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <string.h>

#include "storage_core.h"
#include "storage_format.h"

namespace {

constexpr char kStatePath[] = "/system.dat";
constexpr uint32_t kStateMagic = makeStorageMagic('S', 'T', 'P', 'S');
constexpr uint16_t kStateVersion = 1;
constexpr size_t kStoragePathBufferSize = 64;
constexpr uint8_t kSdPowerPin = 42;
constexpr uint8_t kSdChipSelectPin = 10;
constexpr uint8_t kSdMosiPin = 40;
constexpr uint8_t kSdClockPin = 39;
constexpr uint8_t kSdMisoPin = 13;
constexpr uint32_t kSdFrequencyHz = 4000000;
// An erased flash partition reads as 0xff. Sampling its beginning is only a
// conservative first-use check; non-blank mount failures are never formatted.
constexpr uint8_t kErasedFlashByte = 0xff;
constexpr size_t kBlankPartitionProbeBytes = 64;

struct PersistentState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t bootCount;
};

SPIClass sdSpi(FSPI);
bool littleFsMounted = false;
bool littleFsWritesEnabled = false;
bool sdMounted = false;
bool sdWritesEnabled = false;
uint32_t bootCount = 0;
StorageBackend noticeBackend = StorageBackend::None;
StorageBackend letterBackend = StorageBackend::None;
StorageIssue currentStorageIssue = StorageIssue::None;

bool littleFsReadExact(void*, const char* path, void* destination,
                       size_t size) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.size() != size) {
    if (file) file.close();
    return false;
  }
  const size_t bytesRead = file.read(static_cast<uint8_t*>(destination), size);
  file.close();
  return bytesRead == size;
}

bool littleFsWriteExact(void*, const char* path, const void* data,
                        size_t size) {
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) return false;
  const size_t bytesWritten =
      file.write(static_cast<const uint8_t*>(data), size);
  file.flush();
  const bool succeeded = bytesWritten == size && file.getWriteError() == 0;
  file.close();
  return succeeded;
}

bool littleFsExists(void*, const char* path) { return LittleFS.exists(path); }
bool littleFsRemove(void*, const char* path) { return LittleFS.remove(path); }
bool littleFsRename(void*, const char* from, const char* to) {
  return LittleFS.rename(from, to);
}

const StrawberryCore::StorageOperations kLittleFsOperations{
    nullptr, littleFsReadExact, littleFsWriteExact, littleFsExists,
    littleFsRemove, littleFsRename};

bool validPersistentState(const void* data, size_t size, void*) {
  if (size != sizeof(PersistentState)) return false;
  const PersistentState& state = *static_cast<const PersistentState*>(data);
  return state.magic == kStateMagic && state.version == kStateVersion;
}

bool filesystemPartitionLooksBlank() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (partition == nullptr) return false;
  uint8_t sample[kBlankPartitionProbeBytes];
  if (esp_partition_read(partition, 0, sample, sizeof(sample)) != ESP_OK) {
    return false;
  }
  for (uint8_t value : sample) {
    if (value != kErasedFlashByte) return false;
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
  if (LittleFS.begin(false)) return true;
  if (!filesystemPartitionLooksBlank()) {
    Serial.println(
        "LittleFS mount failed on non-blank storage; refusing to format.");
    return false;
  }
  Serial.println("Blank LittleFS partition detected; formatting for first use.");
  if (!LittleFS.format()) {
    Serial.println("LittleFS format failed.");
    return false;
  }
  return LittleFS.begin(false);
}

bool readSystemState(PersistentState& destination) {
  if (!littleFsMounted) return false;
  const StrawberryCore::StorageLoadSource source =
      StrawberryCore::readStorageWithFallback(
          kLittleFsOperations, kStatePath, "/system.dat.bak", &destination,
          sizeof(destination), validPersistentState);
  if (source == StrawberryCore::StorageLoadSource::Backup &&
      !StrawberryCore::prepareBackupRecovery(kLittleFsOperations,
                                             kStatePath)) {
    return false;
  }
  return source != StrawberryCore::StorageLoadSource::None;
}

bool writeSystemState(const PersistentState& state) {
  return littleFsMounted &&
         StrawberryCore::writeStorageAtomic(
             kLittleFsOperations, kStatePath, "/system.dat.tmp",
             "/system.dat.bak", &state, sizeof(state));
}

bool mountSdCard() {
  pinMode(kSdPowerPin, OUTPUT);
  digitalWrite(kSdPowerPin, HIGH);
  delay(10);
  sdSpi.begin(kSdClockPin, kSdMisoPin, kSdMosiPin, kSdChipSelectPin);
  if (!SD.begin(kSdChipSelectPin, sdSpi, kSdFrequencyHz) ||
      SD.cardType() == CARD_NONE) {
    SD.end();
    sdSpi.end();
    digitalWrite(kSdPowerPin, LOW);
    Serial.println("No usable SD card found at boot.");
    return false;
  }
  return true;
}

StorageBackend backendFor(RecordStorage storage) {
  return storage == RecordStorage::Notices ? noticeBackend : letterBackend;
}

void latchSdReadOnly(const char* reason);

fs::FS* filesystemFor(StorageBackend backend) {
  switch (backend) {
    case StorageBackend::LittleFs:
      return littleFsMounted ? &LittleFS : nullptr;
    case StorageBackend::SdCard:
      return sdMounted ? &SD : nullptr;
    case StorageBackend::None:
      return nullptr;
  }
  return nullptr;
}

void latchSdReadOnly(const char* reason) {
  if (!sdWritesEnabled) return;
  sdWritesEnabled = false;
  currentStorageIssue = StorageIssue::SdFailure;
  Serial.printf("SD record storage is now read-only: %s\n", reason);
  // Records already loaded into bounded RAM remain readable. Stop touching a
  // potentially removed or unhealthy card until the next clean boot.
  SD.end();
  sdSpi.end();
  digitalWrite(kSdPowerPin, LOW);
  sdMounted = false;
}

bool backendWritable(StorageBackend backend) {
  if (backend == StorageBackend::LittleFs) {
    return littleFsMounted && littleFsWritesEnabled;
  }
  if (backend != StorageBackend::SdCard || !sdMounted || !sdWritesEnabled) {
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    latchSdReadOnly("card is no longer present");
    return false;
  }
  return true;
}

bool backendReadable(StorageBackend backend) {
  if (backend == StorageBackend::LittleFs) return littleFsMounted;
  if (backend != StorageBackend::SdCard || !sdMounted) return false;
  if (SD.cardType() == CARD_NONE) {
    latchSdReadOnly("card is no longer present");
    return false;
  }
  return true;
}

bool removeIfPresent(fs::FS& filesystem, const char* path) {
  return !filesystem.exists(path) || filesystem.remove(path);
}

bool readAndValidate(fs::FS& filesystem, const char* path, size_t maximumBytes,
                     StorageJsonReader reader, void* context) {
  File file = filesystem.open(path, FILE_READ);
  if (!file) return false;
  const size_t size = file.size();
  bool valid = size > 0 && size <= maximumBytes && reader(file, context);
  while (valid && file.available()) {
    const int character = file.read();
    valid = character == ' ' || character == '\t' || character == '\r' ||
            character == '\n';
  }
  file.close();
  return valid;
}

bool writeTemporary(fs::FS& filesystem, const char* path,
                    StorageJsonWriter writer, void* context) {
  File file = filesystem.open(path, FILE_WRITE);
  if (!file) return false;
  const bool serialized = writer(file, context);
  file.flush();
  const bool succeeded = serialized && file.getWriteError() == 0;
  file.close();
  return succeeded;
}

bool ensureParentDirectory(fs::FS& filesystem, const char* path) {
  const char* separator = strrchr(path, '/');
  if (separator == nullptr || separator == path) return true;
  char directory[kStoragePathBufferSize];
  const size_t length = separator - path;
  if (length >= sizeof(directory)) return false;
  memcpy(directory, path, length);
  directory[length] = '\0';
  return filesystem.exists(directory) || filesystem.mkdir(directory);
}

void noteBackendFailure(StorageBackend backend, const char* reason) {
  if (backend == StorageBackend::SdCard) {
    latchSdReadOnly(reason);
  } else if (backend == StorageBackend::LittleFs) {
    littleFsWritesEnabled = false;
    currentStorageIssue = StorageIssue::LittleFsFailure;
    Serial.printf("Internal flash record storage is now read-only: %s\n",
                  reason);
  }
}

}  // namespace

bool startStorage() {
  currentStorageIssue = StorageIssue::None;
  sdWritesEnabled = false;
  littleFsWritesEnabled = false;
  // This remains a unique boot-session identifier even if LittleFS is
  // unavailable, which is required to distinguish pending uptime timestamps.
  bootCount = esp_random();
  if (bootCount == 0) bootCount = 1;
  littleFsMounted = mountFilesystem();
  littleFsWritesEnabled = littleFsMounted;
  if (littleFsMounted) {
    PersistentState state{kStateMagic, kStateVersion, 0, 0};
    PersistentState loaded{};
    if (readSystemState(loaded)) {
      state = loaded;
    } else {
      Serial.println("Creating persistent system state.");
    }
    ++state.bootCount;
    bootCount = state.bootCount;
    if (!writeSystemState(state)) {
      Serial.println("Failed to persist system state.");
      littleFsMounted = false;
      littleFsWritesEnabled = false;
      bootCount = esp_random();
      if (bootCount == 0) bootCount = 1;
    }
  } else {
    Serial.println("Internal flash storage unavailable.");
  }

  sdMounted = mountSdCard();
  sdWritesEnabled = sdMounted;
  if (sdMounted) {
    noticeBackend = StorageBackend::SdCard;
    letterBackend = StorageBackend::SdCard;
    Serial.printf("SD record storage ready: %llu / %llu bytes used.\n",
                  static_cast<unsigned long long>(SD.usedBytes()),
                  static_cast<unsigned long long>(SD.totalBytes()));
  } else {
    noticeBackend = StorageBackend::None;
    letterBackend = littleFsMounted ? StorageBackend::LittleFs
                                    : StorageBackend::None;
    Serial.printf("Record storage fallback: notices=%s, letters=%s.\n",
                  storageBackendName(noticeBackend),
                  storageBackendName(letterBackend));
  }
  return storageAvailable();
}

bool storageAvailable() {
  return noticeBackend != StorageBackend::None ||
         letterBackend != StorageBackend::None;
}

StorageBackend recordStorageBackend(RecordStorage storage) {
  return backendFor(storage);
}

const char* storageBackendName(StorageBackend backend) {
  switch (backend) {
    case StorageBackend::LittleFs:
      return "littlefs";
    case StorageBackend::SdCard:
      return "sd";
    case StorageBackend::None:
      return "none";
  }
  return "none";
}

bool recordStorageWritable(RecordStorage storage) {
  return backendWritable(backendFor(storage));
}

bool recordStorageReadable(RecordStorage storage) {
  return backendReadable(backendFor(storage));
}

StorageIssue storageIssue() { return currentStorageIssue; }

const char* storageIssueName(StorageIssue issue) {
  switch (issue) {
    case StorageIssue::None:
      return "none";
    case StorageIssue::SdFailure:
      return "sd-failure";
    case StorageIssue::LittleFsFailure:
      return "littlefs-failure";
  }
  return "none";
}

StorageLoadResult readStorageJsonValidated(
    RecordStorage storage, const char* path, size_t maximumBytes,
    StorageJsonReader reader, void* readerContext) {
  const StorageBackend backend = backendFor(storage);
  if (backend == StorageBackend::None) return StorageLoadResult::Missing;
  if (!backendReadable(backend)) return StorageLoadResult::Invalid;
  fs::FS* filesystem = filesystemFor(backend);
  if (filesystem == nullptr || path == nullptr || maximumBytes == 0 ||
      reader == nullptr) {
    return StorageLoadResult::Invalid;
  }

  char backupPath[kStoragePathBufferSize];
  if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return StorageLoadResult::Invalid;
  }
  const bool primaryExists = filesystem->exists(path);
  const bool backupExists = filesystem->exists(backupPath);
  if (!primaryExists && !backupExists) return StorageLoadResult::Missing;
  if (primaryExists &&
      readAndValidate(*filesystem, path, maximumBytes, reader, readerContext)) {
    return StorageLoadResult::Loaded;
  }
  if (!backupExists || !readAndValidate(*filesystem, backupPath, maximumBytes,
                                        reader, readerContext)) {
    return StorageLoadResult::Invalid;
  }
  if (primaryExists && !filesystem->remove(path)) {
    noteBackendFailure(backend, "could not prepare backup recovery");
  }
  return StorageLoadResult::Loaded;
}

bool writeStorageJsonAtomic(RecordStorage storage, const char* path,
                            StorageJsonWriter writer, void* writerContext) {
  const StorageBackend backend = backendFor(storage);
  fs::FS* filesystem = filesystemFor(backend);
  if (filesystem == nullptr || path == nullptr || writer == nullptr ||
      !backendWritable(backend)) {
    return false;
  }
  if (!ensureParentDirectory(*filesystem, path)) {
    noteBackendFailure(backend, "record directory creation failed");
    return false;
  }

  char temporaryPath[kStoragePathBufferSize];
  char backupPath[kStoragePathBufferSize];
  if (!makeSiblingPath(path, ".tmp", temporaryPath, sizeof(temporaryPath)) ||
      !makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return false;
  }
  if (!removeIfPresent(*filesystem, temporaryPath) ||
      !writeTemporary(*filesystem, temporaryPath, writer, writerContext)) {
    removeIfPresent(*filesystem, temporaryPath);
    noteBackendFailure(backend, "temporary write failed");
    return false;
  }

  const bool hadPrimary = filesystem->exists(path);
  if (hadPrimary && !removeIfPresent(*filesystem, backupPath)) {
    removeIfPresent(*filesystem, temporaryPath);
    noteBackendFailure(backend, "stale backup cleanup failed");
    return false;
  }
  if (hadPrimary && !filesystem->rename(path, backupPath)) {
    removeIfPresent(*filesystem, temporaryPath);
    noteBackendFailure(backend, "primary backup failed");
    return false;
  }
  if (!filesystem->rename(temporaryPath, path)) {
    removeIfPresent(*filesystem, temporaryPath);
    if (hadPrimary && filesystem->exists(backupPath) &&
        !filesystem->exists(path)) {
      filesystem->rename(backupPath, path);
    }
    noteBackendFailure(backend, "temporary promotion failed");
    return false;
  }
  if (!removeIfPresent(*filesystem, backupPath)) {
    noteBackendFailure(backend, "committed backup cleanup failed");
  }
  return true;
}

bool visitStorageFiles(RecordStorage storage, const char* directory,
                       StorageFileVisitor visitor, void* visitorContext) {
  const StorageBackend backend = backendFor(storage);
  if (!backendReadable(backend)) return false;
  fs::FS* filesystem = filesystemFor(backend);
  if (filesystem == nullptr || directory == nullptr || visitor == nullptr) {
    return false;
  }
  File folder = filesystem->open(directory, FILE_READ);
  if (!folder || !folder.isDirectory()) {
    if (folder) folder.close();
    return true;
  }
  File entry = folder.openNextFile();
  while (entry) {
    char path[kStoragePathBufferSize];
    const bool usable = !entry.isDirectory() &&
                        strlcpy(path, entry.path(), sizeof(path)) < sizeof(path);
    entry.close();
    bool visit = usable;
    if (visit) {
      const size_t length = strlen(path);
      constexpr char kBackupSuffix[] = ".json.bak";
      constexpr char kJsonSuffix[] = ".json";
      if (length > sizeof(kBackupSuffix) - 1 &&
          strcmp(path + length - (sizeof(kBackupSuffix) - 1),
                 kBackupSuffix) == 0) {
        path[length - 4] = '\0';
        // A present primary will be visited separately. A backup-only record
        // is exposed under its logical primary path so validated recovery can
        // load it without duplicate page entries.
        visit = !filesystem->exists(path);
      } else if (length <= sizeof(kJsonSuffix) - 1 ||
                 strcmp(path + length - (sizeof(kJsonSuffix) - 1),
                        kJsonSuffix) != 0) {
        visit = false;
      }
    }
    if (visit && !visitor(path, visitorContext)) {
      folder.close();
      return false;
    }
    entry = folder.openNextFile();
  }
  folder.close();
  return true;
}

bool removeStorageFile(RecordStorage storage, const char* path) {
  const StorageBackend backend = backendFor(storage);
  fs::FS* filesystem = filesystemFor(backend);
  if (filesystem == nullptr || path == nullptr || !backendWritable(backend)) {
    return false;
  }
  char backupPath[kStoragePathBufferSize];
  char temporaryPath[kStoragePathBufferSize];
  if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath)) ||
      !makeSiblingPath(path, ".tmp", temporaryPath, sizeof(temporaryPath)) ||
      !removeIfPresent(*filesystem, backupPath) ||
      !removeIfPresent(*filesystem, temporaryPath) ||
      !removeIfPresent(*filesystem, path)) {
    noteBackendFailure(backend, "record removal failed");
    return false;
  }
  return true;
}

bool storageFileExists(RecordStorage storage, const char* path) {
  const StorageBackend backend = backendFor(storage);
  if (!backendReadable(backend)) return false;
  fs::FS* filesystem = filesystemFor(backend);
  if (filesystem == nullptr || path == nullptr) return false;
  if (filesystem->exists(path)) return true;
  char backupPath[kStoragePathBufferSize];
  return makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath)) &&
         filesystem->exists(backupPath);
}

uint32_t persistedBootCount() { return bootCount; }

uint64_t storageUsedBytes() {
  if (noticeBackend == StorageBackend::SdCard ||
      letterBackend == StorageBackend::SdCard) {
    return sdMounted ? SD.usedBytes() : 0;
  }
  return littleFsMounted ? LittleFS.usedBytes() : 0;
}

uint64_t storageTotalBytes() {
  if (noticeBackend == StorageBackend::SdCard ||
      letterBackend == StorageBackend::SdCard) {
    return sdMounted ? SD.totalBytes() : 0;
  }
  return littleFsMounted ? LittleFS.totalBytes() : 0;
}
