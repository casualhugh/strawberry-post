#include <map>
#include <string>
#include <vector>

#include <string.h>
#include <unity.h>

#include "storage_core.h"

namespace {

struct FakeStorage {
  std::map<std::string, std::vector<uint8_t> > files;
  std::map<std::string, size_t> failures;

  bool shouldFail(const std::string& operation) {
    std::map<std::string, size_t>::iterator found = failures.find(operation);
    if (found == failures.end() || found->second == 0) {
      return false;
    }
    --found->second;
    return true;
  }

  void failOnce(const std::string& operation) {
    failures[operation] = 1;
  }
};

bool fakeRead(void* context, const char* path, void* destination, size_t size) {
  FakeStorage& storage = *static_cast<FakeStorage*>(context);
  if (storage.shouldFail(std::string("read:") + path)) {
    return false;
  }
  const std::map<std::string, std::vector<uint8_t> >::const_iterator found =
      storage.files.find(path);
  if (found == storage.files.end() || found->second.size() != size) {
    return false;
  }
  memcpy(destination, &found->second[0], size);
  return true;
}

bool fakeWrite(void* context, const char* path, const void* data, size_t size) {
  FakeStorage& storage = *static_cast<FakeStorage*>(context);
  if (storage.shouldFail(std::string("write:") + path)) {
    return false;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  storage.files[path] = std::vector<uint8_t>(bytes, bytes + size);
  return true;
}

bool fakeExists(void* context, const char* path) {
  FakeStorage& storage = *static_cast<FakeStorage*>(context);
  return storage.files.find(path) != storage.files.end();
}

bool fakeRemove(void* context, const char* path) {
  FakeStorage& storage = *static_cast<FakeStorage*>(context);
  if (storage.shouldFail(std::string("remove:") + path)) {
    return false;
  }
  return storage.files.erase(path) == 1;
}

bool fakeRename(void* context, const char* from, const char* to) {
  FakeStorage& storage = *static_cast<FakeStorage*>(context);
  if (storage.shouldFail(std::string("rename:") + from + "->" + to)) {
    return false;
  }
  std::map<std::string, std::vector<uint8_t> >::iterator source =
      storage.files.find(from);
  if (source == storage.files.end() || storage.files.find(to) != storage.files.end()) {
    return false;
  }
  storage.files[to] = source->second;
  storage.files.erase(source);
  return true;
}

StrawberryCore::StorageOperations operationsFor(FakeStorage& storage) {
  StrawberryCore::StorageOperations operations = {
      &storage, fakeRead, fakeWrite, fakeExists, fakeRemove, fakeRename};
  return operations;
}

struct Snapshot {
  uint32_t magic;
  uint16_t version;
  uint16_t value;
};

// "STPS" is a fixture's application signature, not a LittleFS requirement.
const uint32_t kMagic = UINT32_C(0x53545053);

bool validSnapshot(const void* data, size_t size, void*) {
  if (size != sizeof(Snapshot)) {
    return false;
  }
  const Snapshot& snapshot = *static_cast<const Snapshot*>(data);
  return snapshot.magic == kMagic && snapshot.version == 1;
}

template <typename Value>
void put(FakeStorage& storage, const char* path, const Value& value) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
  storage.files[path] =
      std::vector<uint8_t>(bytes, bytes + sizeof(Value));
}

Snapshot getSnapshot(const FakeStorage& storage, const char* path) {
  Snapshot result = {};
  const std::map<std::string, std::vector<uint8_t> >::const_iterator found =
      storage.files.find(path);
  if (found != storage.files.end() && found->second.size() == sizeof(result)) {
    memcpy(&result, &found->second[0], sizeof(result));
  }
  return result;
}

StrawberryCore::StorageLoadSource loadSnapshot(FakeStorage& storage,
                                               Snapshot& output) {
  return StrawberryCore::readStorageWithFallback(
      operationsFor(storage), "/state", "/state.bak", &output,
      sizeof(output), validSnapshot);
}

bool readSnapshot(FakeStorage& storage, Snapshot& output) {
  return loadSnapshot(storage, output) !=
         StrawberryCore::StorageLoadSource::None;
}

bool writeSnapshot(FakeStorage& storage, const Snapshot& value) {
  return StrawberryCore::writeStorageAtomic(
      operationsFor(storage), "/state", "/state.tmp", "/state.bak",
      &value, sizeof(value));
}

void assertFileValue(const FakeStorage& storage, const char* path,
                     uint16_t expected) {
  TEST_ASSERT_TRUE(storage.files.find(path) != storage.files.end());
  TEST_ASSERT_EQUAL_UINT16(expected, getSnapshot(storage, path).value);
}

void test_valid_primary_is_loaded() {
  FakeStorage storage;
  const Snapshot primary{kMagic, 1, 7};
  const Snapshot backup{kMagic, 1, 8};
  put(storage, "/state", primary);
  put(storage, "/state.bak", backup);
  Snapshot loaded = {};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(StrawberryCore::StorageLoadSource::Primary),
      static_cast<uint8_t>(loadSnapshot(storage, loaded)));
  TEST_ASSERT_EQUAL_UINT16(7, loaded.value);
}

void test_exact_sized_invalid_primary_falls_back_to_valid_backup() {
  FakeStorage storage;
  const Snapshot corrupt{UINT32_C(0xdeadbeef), 1, 7};
  const Snapshot backup{kMagic, 1, 42};
  put(storage, "/state", corrupt);
  put(storage, "/state.bak", backup);
  Snapshot loaded = {};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(StrawberryCore::StorageLoadSource::Backup),
      static_cast<uint8_t>(loadSnapshot(storage, loaded)));
  TEST_ASSERT_EQUAL_UINT16(42, loaded.value);
}

void test_truncated_primary_falls_back_and_both_invalid_fail() {
  FakeStorage storage;
  storage.files["/state"] = std::vector<uint8_t>(3, 0xaa);
  const Snapshot backup{kMagic, 1, 19};
  put(storage, "/state.bak", backup);
  Snapshot loaded = {};
  TEST_ASSERT_TRUE(readSnapshot(storage, loaded));
  TEST_ASSERT_EQUAL_UINT16(19, loaded.value);

  const Snapshot corrupt{kMagic, 99, 20};
  put(storage, "/state.bak", corrupt);
  TEST_ASSERT_FALSE(readSnapshot(storage, loaded));
}

void test_atomic_write_replaces_primary_and_cleans_siblings() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  TEST_ASSERT_TRUE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 2);
  TEST_ASSERT_TRUE(storage.files.find("/state.tmp") == storage.files.end());
  TEST_ASSERT_TRUE(storage.files.find("/state.bak") == storage.files.end());
}

void test_write_failure_leaves_old_primary_readable() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  storage.failOnce("write:/state.tmp");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 1);
  Snapshot loaded = {};
  TEST_ASSERT_TRUE(readSnapshot(storage, loaded));
  TEST_ASSERT_EQUAL_UINT16(1, loaded.value);
}

void test_stale_backup_remove_failure_does_not_touch_primary() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot staleBackup{kMagic, 1, 9};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  put(storage, "/state.bak", staleBackup);
  storage.failOnce("remove:/state.bak");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 1);
  assertFileValue(storage, "/state.bak", 9);
}

void test_primary_rename_failure_leaves_old_primary() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  storage.failOnce("rename:/state->/state.bak");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 1);
  TEST_ASSERT_TRUE(storage.files.find("/state.tmp") == storage.files.end());
}

void test_promotion_failure_restores_old_primary() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  storage.failOnce("rename:/state.tmp->/state");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 1);
  TEST_ASSERT_TRUE(storage.files.find("/state.bak") == storage.files.end());
}

void test_failed_restore_still_leaves_backup_readable() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  storage.failOnce("rename:/state.tmp->/state");
  storage.failOnce("rename:/state.bak->/state");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  TEST_ASSERT_TRUE(storage.files.find("/state") == storage.files.end());
  assertFileValue(storage, "/state.bak", 1);
  Snapshot loaded = {};
  TEST_ASSERT_TRUE(readSnapshot(storage, loaded));
  TEST_ASSERT_EQUAL_UINT16(1, loaded.value);
}

void test_backup_only_recovery_survives_promotion_failure() {
  FakeStorage storage;
  const Snapshot backup{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state.bak", backup);
  storage.failOnce("rename:/state.tmp->/state");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  TEST_ASSERT_TRUE(storage.files.find("/state") == storage.files.end());
  assertFileValue(storage, "/state.bak", 1);
  Snapshot loaded = {};
  TEST_ASSERT_TRUE(readSnapshot(storage, loaded));
  TEST_ASSERT_EQUAL_UINT16(1, loaded.value);
}

void test_invalid_primary_recovery_preserves_backup_on_promotion_failure() {
  FakeStorage storage;
  const Snapshot corrupt{UINT32_C(0xdeadbeef), 1, 99};
  const Snapshot backup{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", corrupt);
  put(storage, "/state.bak", backup);

  Snapshot loaded = {};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(StrawberryCore::StorageLoadSource::Backup),
      static_cast<uint8_t>(loadSnapshot(storage, loaded)));
  TEST_ASSERT_EQUAL_UINT16(1, loaded.value);
  TEST_ASSERT_TRUE(StrawberryCore::prepareBackupRecovery(
      operationsFor(storage), "/state"));
  TEST_ASSERT_TRUE(storage.files.find("/state") == storage.files.end());
  assertFileValue(storage, "/state.bak", 1);

  storage.failOnce("rename:/state.tmp->/state");
  TEST_ASSERT_FALSE(writeSnapshot(storage, newValue));
  TEST_ASSERT_TRUE(storage.files.find("/state") == storage.files.end());
  assertFileValue(storage, "/state.bak", 1);

  loaded = {};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(StrawberryCore::StorageLoadSource::Backup),
      static_cast<uint8_t>(loadSnapshot(storage, loaded)));
  TEST_ASSERT_EQUAL_UINT16(1, loaded.value);
}

void test_failed_recovery_preparation_preserves_both_candidates() {
  FakeStorage storage;
  const Snapshot corrupt{UINT32_C(0xdeadbeef), 1, 99};
  const Snapshot backup{kMagic, 1, 1};
  put(storage, "/state", corrupt);
  put(storage, "/state.bak", backup);
  storage.failOnce("remove:/state");

  TEST_ASSERT_FALSE(StrawberryCore::prepareBackupRecovery(
      operationsFor(storage), "/state"));
  assertFileValue(storage, "/state", 99);
  assertFileValue(storage, "/state.bak", 1);
}

void test_backup_cleanup_failure_keeps_new_primary_committed() {
  FakeStorage storage;
  const Snapshot oldValue{kMagic, 1, 1};
  const Snapshot newValue{kMagic, 1, 2};
  put(storage, "/state", oldValue);
  storage.failOnce("remove:/state.bak");
  TEST_ASSERT_TRUE(writeSnapshot(storage, newValue));
  assertFileValue(storage, "/state", 2);
  assertFileValue(storage, "/state.bak", 1);
  Snapshot loaded = {};
  TEST_ASSERT_TRUE(readSnapshot(storage, loaded));
  TEST_ASSERT_EQUAL_UINT16(2, loaded.value);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_primary_is_loaded);
  RUN_TEST(test_exact_sized_invalid_primary_falls_back_to_valid_backup);
  RUN_TEST(test_truncated_primary_falls_back_and_both_invalid_fail);
  RUN_TEST(test_atomic_write_replaces_primary_and_cleans_siblings);
  RUN_TEST(test_write_failure_leaves_old_primary_readable);
  RUN_TEST(test_stale_backup_remove_failure_does_not_touch_primary);
  RUN_TEST(test_primary_rename_failure_leaves_old_primary);
  RUN_TEST(test_promotion_failure_restores_old_primary);
  RUN_TEST(test_failed_restore_still_leaves_backup_readable);
  RUN_TEST(test_backup_only_recovery_survives_promotion_failure);
  RUN_TEST(
      test_invalid_primary_recovery_preserves_backup_on_promotion_failure);
  RUN_TEST(test_failed_recovery_preparation_preserves_both_candidates);
  RUN_TEST(test_backup_cleanup_failure_keeps_new_primary_committed);
  return UNITY_END();
}
