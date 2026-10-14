#pragma once

#include <Arduino.h>

// Small binary records in LittleFS, guarded by a magic, a version and a CRC32.
// A record with a different version or size is treated as missing.
namespace Storage {
    bool save(const char* path, uint32_t magic, uint16_t version, const void* data, size_t size);
    bool load(const char* path, uint32_t magic, uint16_t version, void* data, size_t size);
}
