#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

enum class RecordStorage : uint8_t { Notices, Letters };
enum class StorageBackend : uint8_t { None, LittleFs, SdCard };
enum class StorageLoadResult : uint8_t { Missing, Loaded, Invalid };
enum class StorageIssue : uint8_t { None, SdFailure, LittleFsFailure };

using StorageJsonReader = bool (*)(Stream& input, void* context);
using StorageJsonWriter = bool (*)(Print& output, void* context);
using StorageFileVisitor = bool (*)(const char* path, void* context);

bool startStorage();
bool storageAvailable();
StorageBackend recordStorageBackend(RecordStorage storage);
const char* storageBackendName(StorageBackend backend);
bool recordStorageReadable(RecordStorage storage);
bool recordStorageWritable(RecordStorage storage);
StorageIssue storageIssue();
const char* storageIssueName(StorageIssue issue);
StorageLoadResult readStorageJsonValidated(
    RecordStorage storage, const char* path, size_t maximumBytes,
    StorageJsonReader reader, void* readerContext = nullptr);
bool writeStorageJsonAtomic(RecordStorage storage, const char* path,
                            StorageJsonWriter writer,
                            void* writerContext = nullptr);
bool visitStorageFiles(RecordStorage storage, const char* directory,
                       StorageFileVisitor visitor,
                       void* visitorContext = nullptr);
bool removeStorageFile(RecordStorage storage, const char* path);
bool storageFileExists(RecordStorage storage, const char* path);
uint32_t persistedBootCount();
uint64_t storageUsedBytes();
uint64_t storageTotalBytes();
