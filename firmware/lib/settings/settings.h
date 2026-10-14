#pragma once

#include <Arduino.h>

// Runtime-tunable motion settings.
// Defaults come from config.h. The configurator page changes them over USB serial,
// save() persists them to LittleFS, and saved values override config.h on boot.
// Core 0 owns the values; Core 1 reads a consistent copy through fetch().
namespace Settings {
    struct Values {
        float deadzone_translation;  // normalized x/y/z magnitude
        float deadzone_rotation;     // normalized rx/ry/rz magnitude
        float cross_axis_ratio;      // 0..<1, 0 = off
        float isolation_power;       // > 0, 1 = no curve
        float auto_zero_time_constant_s; // 0 = off
        float translation_max[3];    // mm for normalized +1 (x, y, z)
        float translation_min[3];    // mm magnitude for normalized -1
        float rotation_max[3];       // degrees for normalized +1 (rx, ry, rz)
        float rotation_min[3];       // degrees magnitude for normalized -1
        float ekf_process_noise_std; // higher = more responsive/noisy
        float ekf_sensor_noise_std;  // higher = smoother/less sensor trust
        float sleep_timeout_s;       // inactivity until sleep, 0 = never
    };

    Values defaults();

    // Load saved values (or defaults) and publish them. Call after LittleFS is mounted.
    void begin();

    // Core 0 view of the current values.
    const Values& get();

    // Core 0: make new values visible to both cores.
    void publish(const Values& values);

    // Core 1: copy the values if they changed since last_sequence.
    bool fetch(uint32_t& last_sequence, Values& out);

    // Set one value by its configurator key, clamped to its valid range.
    bool set(const char* key, float value);

    // One JSON line: {"t":"settings","v":{"key":value,...}}
    void print_json(Print& out);

    bool save();
    bool load(Values& out);
}
