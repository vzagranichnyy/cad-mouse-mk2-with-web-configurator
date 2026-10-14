#include "storage.h"

#include <LittleFS.h>

namespace Storage {
    namespace {
        struct Header {
            uint32_t magic;
            uint16_t version;
            uint16_t size;
            uint32_t crc;
        };

        uint32_t crc32(const uint8_t* data, size_t length)
        {
            uint32_t crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < length; ++i) {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
                }
            }
            return ~crc;
        }
    } // namespace

    bool save(const char* path, uint32_t magic, uint16_t version, const void* data, size_t size)
    {
        Header header;
        header.magic = magic;
        header.version = version;
        header.size = static_cast<uint16_t>(size);
        header.crc = crc32(static_cast<const uint8_t*>(data), size);

        File file = LittleFS.open(path, "w");
        if (!file) {
            return false;
        }
        size_t written = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
        written += file.write(static_cast<const uint8_t*>(data), size);
        file.close();

        return written == sizeof(header) + size;
    }

    bool load(const char* path, uint32_t magic, uint16_t version, void* data, size_t size)
    {
        File file = LittleFS.open(path, "r");
        if (!file) {
            return false;
        }

        Header header;
        uint8_t buffer[512];
        if (size > sizeof(buffer)) {
            file.close();
            return false;
        }
        size_t read = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));
        read += file.read(buffer, size);
        file.close();

        if (read != sizeof(header) + size ||
            header.magic != magic ||
            header.version != version ||
            header.size != size ||
            header.crc != crc32(buffer, size)) {
            return false;
        }

        memcpy(data, buffer, size);
        return true;
    }
} // namespace Storage
