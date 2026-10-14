#pragma once

#include <Arduino.h>

// Linear cross-axis decoupling of the estimated pose.
// The guided calibration in tools/configurator.html records how the other axes
// respond while the knob is moved along one axis, fits a 6x6 matrix that removes
// that response, and sends it here. It is applied in physical units (mm, rad)
// before normalization, so it does not depend on the sensitivity settings.
// Core 0 owns the matrix; Core 1 reads a consistent copy through fetch().
namespace Decoupling {
    struct Matrix {
        float m[6][6];    // corrected pose = m * pose (x, y, z, rx, ry, rz)
        uint32_t enabled; // 0 = not applied
    };

    Matrix identity();

    // Load the saved matrix (or identity, disabled) and publish it. Call after LittleFS is mounted.
    void begin();

    const Matrix& get();
    void publish(const Matrix& matrix);
    bool fetch(uint32_t& last_sequence, Matrix& out);

    // Apply in place to the pose and, with the same matrix, to its velocity.
    void apply(const Matrix& matrix, float pose[6], float velocity[6]);

    // One JSON line: {"t":"dc","on":0|1,"m":[36 values, row by row]}
    void print_json(Print& out);

    bool save();
    bool load(Matrix& out);
}
