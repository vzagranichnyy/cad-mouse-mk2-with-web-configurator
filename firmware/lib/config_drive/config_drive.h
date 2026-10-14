#pragma once

#include <Arduino.h>

// Read-only USB drive with tools/configurator.html, next to the HID and serial interfaces.
// It is shown only when a button is held while the mouse is plugged in, or after
// reboot_into_drive(). The disk image is generated from the page at build time by
// tools/make_drive_image.py (drive_image.h).
namespace ConfigDrive {
    // Call in setup() before the HID interface starts, so the drive is part of the enumeration.
    // Returns true when drive mode is active.
    bool begin(uint8_t pin_left_button, uint8_t pin_right_button);

    bool active();

    // Reboot, and show the drive after the reboot.
    void reboot_into_drive();
}
