#pragma once

#include <Arduino.h>

// Nomad mode: the knob drives Nomad Sculpt's own mouse navigation instead of acting as a 3D mouse.
//
// Nomad (desktop) binds view_rotate to a right-button drag and view_pan to a middle-button drag,
// both with force_camera, and zooms with the wheel. Its built-in 3D mouse support and the 3DxWare
// mouse emulation both act on the 3D mouse interface at the same time and fight each other, so in
// this mode the 3D mouse interface stays silent and the knob produces those mouse gestures instead.
//
// A drag moves the real cursor. It is kept within RECENTER_SPAN_PX of where the gesture started:
// when it gets further, the button is released, the cursor moved back and the button pressed again,
// so a continuous rotation never hits a screen edge. After the gesture the cursor is moved back.
//
// Runs on Core 0 from the CAD output (normalized, after deadzones and isolation).
// The configuration is stored in LittleFS; the mode itself always starts disabled.
namespace NomadMode {
    enum Target : uint8_t {
        NONE,
        ROTATE_H, // positive: drag right
        ROTATE_V, // positive: drag up
        PAN_H,    // positive: drag right
        PAN_V,    // positive: drag up
        ZOOM,     // positive: wheel up (zoom in)
        TARGET_COUNT,
    };

    enum class Gesture : uint8_t {
        NONE,
        ROTATE,
        PAN,
    };

    struct Config {
        uint8_t target[6];          // per axis x, y, z, rx, ry, rz
        int8_t sign[6];             // +1 or -1 per axis
        float rotate_speed;         // pixels per second at full deflection
        float pan_speed;            // pixels per second at full deflection
        float zoom_speed;           // wheel notches per second at full deflection
        uint8_t rotate_button;      // HID mouse button bits: 1 left, 2 right, 4 middle
        uint8_t pan_button;
        uint8_t rotate_modifier;    // HID keyboard modifier bits held during the drag: 1 Ctrl, 2 Shift, 4 Alt
        uint8_t pan_modifier;
        uint8_t button_modifier[2]; // knob buttons (left, right): shortcut sent on a short press
        uint8_t button_key[2];      // HID keyboard usage ID, 0 = none
        uint8_t restore_cursor;     // 1 = move the cursor back after a gesture
        uint8_t reserved[3];
    };

    // A report the HID layer should send now, if it can.
    struct Report {
        enum class Kind : uint8_t {
            NONE,
            MOUSE,
            KEYBOARD,
        } kind;
        uint8_t buttons;
        int8_t dx;
        int8_t dy;
        int8_t wheel;
        uint8_t modifier;
        uint8_t key;
    };

    Config defaults();

    // Load the saved configuration (or defaults). Call after LittleFS is mounted.
    void begin();

    const Config& get();
    void set(const Config& config);
    bool save();
    bool load(Config& out);

    bool enabled();
    // Takes effect at once; a running gesture is still finished (button released, cursor moved back),
    // so keep calling update() and next_report() while busy().
    void set_enabled(bool enabled);

    // Feed the current CAD output (6 normalized axes) once per loop.
    void update(uint32_t now_us, const float output[6]);

    // Queue the shortcut of a knob button (0 left, 1 right) for a short press.
    void tap_button(int index);

    // Next report to send; call report_sent() after the HID layer accepted it.
    Report next_report();
    void report_sent();

    // True while a gesture, a cursor return or a shortcut is still being sent.
    bool busy();

    Gesture gesture();

#ifdef NOMAD_MODE_SELFTEST
    void self_test(Print& out);
#endif

    // One JSON line: {"t":"nm","on":..,"tg":[6],"sg":[6],"rs":..,"ps":..,"zs":..,"rb":..,"pb":..,"rm":..,"pm":..,"bm":[2],"bk":[2],"rc":..}
    void print_json(Print& out);
}
