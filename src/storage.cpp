#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace {

constexpr char kStatePath[] = "/system.dat";
constexpr uint32_t kStateMagic = 0x53545053;  // "STPS"
constexpr uint16_t kStateVersion = 1;

struct PersistentState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t bootCount;
};

bool mounted = false;
uint32_t bootCount = 0;

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

  Serial.println("LittleFS mount failed; attempting first-use format.");
  if (!LittleFS.format()) {
    Serial.println("LittleFS format failed.");
    return false;
  }
  return LittleFS.begin(false);
}

}  // namespace

bool readStorageFile(const char* path, void* destination, size_t size) {
  if (!mounted || path == nullptr || destination == nullptr || size == 0) {
    return false;
  }

  char backupPath[48];
  if (!makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return false;
  }

  const char* readablePath = LittleFS.exists(path) ? path : backupPath;
  File file = LittleFS.open(readablePath, FILE_READ);
  if (!file || file.size() != size) {
    if (file) {
      file.close();
    }
    return false;
  }

  const size_t bytesRead = file.read(static_cast<uint8_t*>(destination), size);
  file.close();
  return bytesRead == size;
}

bool writeStorageFileAtomic(const char* path, const void* data, size_t size) {
  if (!mounted || path == nullptr || data == nullptr || size == 0) {
    return false;
  }

  char temporaryPath[48];
  char backupPath[48];
  if (!makeSiblingPath(path, ".tmp", temporaryPath, sizeof(temporaryPath)) ||
      !makeSiblingPath(path, ".bak", backupPath, sizeof(backupPath))) {
    return false;
  }

  LittleFS.remove(temporaryPath);
  File file = LittleFS.open(temporaryPath, FILE_WRITE);
  if (!file) {
    return false;
  }

  const size_t bytesWritten =
      file.write(static_cast<const uint8_t*>(data), size);
  file.flush();
  file.close();
  if (bytesWritten != size) {
    LittleFS.remove(temporaryPath);
    return false;
  }

  LittleFS.remove(backupPath);
  if (LittleFS.exists(path) && !LittleFS.rename(path, backupPath)) {
      LittleFS.remove(temporaryPath);
      return false;
  }
  if (!LittleFS.rename(temporaryPath, path)) {
    LittleFS.remove(temporaryPath);
    if (LittleFS.exists(backupPath)) {
      LittleFS.rename(backupPath, path);
    }
    return false;
  }
  LittleFS.remove(backupPath);
  return true;
}

bool startStorage() {
  mounted = mountFilesystem();
  if (!mounted) {
    Serial.println("Persistent storage unavailable; continuing without it.");
    return false;
  }

  PersistentState state{kStateMagic, kStateVersion, 0, 0};
  PersistentState loaded{};
  if (readStorageFile(kStatePath, &loaded, sizeof(loaded)) &&
      loaded.magic == kStateMagic && loaded.version == kStateVersion) {
    state = loaded;
  } else {
    Serial.println("Creating persistent system state.");
  }

  ++state.bootCount;
  bootCount = state.bootCount;
  if (!writeStorageFileAtomic(kStatePath, &state, sizeof(state))) {
    Serial.println("Failed to persist system state.");
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
