#pragma once

#include <stddef.h>
#include <stdint.h>

bool startStorage();
bool storageAvailable();
bool readStorageFile(const char* path, void* destination, size_t size);
bool writeStorageFileAtomic(const char* path, const void* data, size_t size);
uint32_t persistedBootCount();
