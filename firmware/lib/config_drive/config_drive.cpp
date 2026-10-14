#include "config_drive.h"

#include <Adafruit_TinyUSB.h>
#include <hardware/watchdog.h>
#include <string.h>

#include "drive_image.h"

namespace ConfigDrive {
    namespace {
        // Watchdog scratch registers survive a watchdog reboot; scratch[4..7] belong to the SDK.
        const uint32_t DRIVE_MODE_MAGIC = 0x44524956u; // "DRIV"
        const uint16_t BLOCK_SIZE = 512;

        Adafruit_USBD_MSC g_msc;
        bool g_active = false;

        int32_t read_blocks(uint32_t lba, void* buffer, uint32_t size)
        {
            // The image stores the used part of the disk; everything after it reads as zeros.
            uint8_t* out = static_cast<uint8_t*>(buffer);
            const uint32_t offset = lba * BLOCK_SIZE;
            uint32_t copied = 0;
            if (offset < DRIVE_IMAGE_SIZE) {
                copied = DRIVE_IMAGE_SIZE - offset;
                if (copied > size) {
                    copied = size;
                }
                memcpy(out, DRIVE_IMAGE + offset, copied);
            }
            memset(out + copied, 0, size - copied);
            return static_cast<int32_t>(size);
        }

        int32_t write_blocks(uint32_t, uint8_t*, uint32_t)
        {
            return -1; // Read-only
        }

        void flush_blocks() {}

        bool is_writable()
        {
            return false;
        }
    } // namespace

    bool begin(uint8_t pin_left_button, uint8_t pin_right_button)
    {
        const bool requested = watchdog_hw->scratch[0] == DRIVE_MODE_MAGIC;
        watchdog_hw->scratch[0] = 0;

        pinMode(pin_left_button, INPUT_PULLUP);
        pinMode(pin_right_button, INPUT_PULLUP);
        delay(2); // Let the pull-ups settle
        const bool button_held = digitalRead(pin_left_button) == LOW || digitalRead(pin_right_button) == LOW;

        if (!requested && !button_held) {
            return false;
        }

        g_msc.setID("CADMouse", "Configurator", "1.0");
        g_msc.setCapacity(DRIVE_BLOCK_COUNT, BLOCK_SIZE);
        g_msc.setReadWriteCallback(read_blocks, write_blocks, flush_blocks);
        g_msc.setWritableCallback(is_writable);
        g_msc.setUnitReady(true);
        g_msc.begin();

        g_active = true;
        return true;
    }

    bool active()
    {
        return g_active;
    }

    void reboot_into_drive()
    {
        watchdog_hw->scratch[0] = DRIVE_MODE_MAGIC;
        rp2040.reboot();
    }
} // namespace ConfigDrive
