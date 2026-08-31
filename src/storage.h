#pragma once

#include <stddef.h>
#include <stdint.h>

using StorageFileValidator = bool (*)(const void* data, size_t size,
                                      void* context);

bool startStorage();
bool storageAvailable();
bool readStorageFileValidated(const char* path, void* destination, size_t size,
                              StorageFileValidator validator,
                              void* validatorContext = nullptr);
bool writeStorageFileAtomic(const char* path, const void* data, size_t size);
uint32_t persistedBootCount();
size_t storageUsedBytes();
size_t storageTotalBytes();
