#pragma once

#include <Arduino.h>

// Game mode: the knob and the buttons press keyboard keys instead of acting as a 3D mouse,
// e.g. WASD for an Android emulator whose key mapping drives the on-screen joystick.
// Runs on Core 0 from the normalized pose (after decoupling, before deadzones).
// The key bindings are stored in LittleFS; game mode itself always starts disabled.
namespace GameMode {
    enum Binding : uint8_t {
        X_POS, // right
        X_NEG, // left
        Y_POS, // forward
        Y_NEG, // backward
        Z_POS, // pull up
        Z_NEG, // press down
        RX_POS, // tilt toward you
        RX_NEG, // tilt away
        RY_POS, // tilt right
        RY_NEG, // tilt left
        RZ_POS, // turn counter-clockwise
        RZ_NEG, // turn clockwise
        BUTTON_LEFT,
        BUTTON_RIGHT,
        BINDING_COUNT,
    };

    struct Config {
        float translation_threshold; // normalized x/y/z that presses the key
        float rotation_threshold;    // normalized rx/ry/rz that presses the key
        uint8_t keys[BINDING_COUNT]; // HID keyboard usage IDs, 0 = none, 0xE0..0xE7 = modifiers
        uint8_t reserved[2];
    };

    Config defaults();

    // Load saved bindings (or defaults). Call after LittleFS is mounted.
    void begin();

    const Config& get();
    void set(const Config& config);
    bool save();
    bool load(Config& out);

    bool enabled();
    void set_enabled(bool enabled);

    // Keyboard report for the current pose and buttons; all keys released while disabled.
    void make_report(const float pose[6], uint16_t buttons, uint8_t& modifier, uint8_t keys[6]);

    // One bit per binding that is currently held, for the configurator.
    uint16_t active_mask();

    // One JSON line: {"t":"gm","on":0|1,"tt":..,"rt":..,"k":[14 key codes]}
    void print_json(Print& out);
}
