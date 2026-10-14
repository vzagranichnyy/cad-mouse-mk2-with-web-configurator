#pragma once

#include "config.h"

#include <Adafruit_TinyUSB.h>
#include <Arduino.h>

class HIDController {
public:
    struct ReportAxes {
        int16_t x;
        int16_t y;
        int16_t z;
        int16_t rx;
        int16_t ry;
        int16_t rz;
    };

    struct ReportButtons {
        uint16_t bits;
    };

    bool begin();
    void task();
    void sendReport(float filtered_state[12], uint16_t buttons, bool forceUpdate);

    // Keyboard interface for game mode; sends only when the keys change.
    void sendKeyboard(uint8_t modifier, const uint8_t keys[6]);

    uint32_t get_last_report_time_ms();

private:
    Adafruit_USBD_HID m_hid;
    Adafruit_USBD_HID m_keyboard;
    uint8_t m_keyboard_modifier{0};
    uint8_t m_keyboard_keys[6]{};
    bool m_keyboard_pending{false}; // last change not sent yet because the endpoint was busy

    uint32_t m_last_sent_time_ms{0}; // Timestamp of the last sent report
    ReportAxes m_report_axes;
    ReportButtons m_report_buttons;

    ReportAxes makeReportAxes(float filtered_state[12]);
    ReportButtons makeReportButtons(uint16_t buttons);

    bool axesChanged(const ReportAxes& new_axes, const uint32_t now);
    bool buttonsChanged(const ReportButtons& new_buttons);
};