#pragma once

#include <Arduino.h>

#include "dipole_model.h"

// Line-based text protocol over USB serial for tools/configurator.html.
// Settings:   get | set <key> <value> | save | defaults | reload
// Decoupling: dc_get | dc_set <36 values> | dc_on <0|1> | dc_save | dc_reload | dc_reset
// Game mode:  gm_get | gm_set <tt> <rt> <14 key codes> | gm_on <0|1> | gm_save | gm_defaults
// Other:      info | stream <0|1|2> | calibrate | drive | reboot
// Replies and telemetry are single-line JSON objects with a "t" type field.
namespace Configurator {
    enum class Request {
        NONE,
        CALIBRATE,
        GAME_MODE_ON,
        GAME_MODE_OFF,
    };

    struct Telemetry {
        int state;             // StateMachine::State
        int calibration_state; // Calibration::LoadState
        const float* pose;     // 6 values, normalized pose before deadzones
        const float* raw_pose; // 6 values, physical pose before decoupling (mm, rad)
        const float* output;   // 6 values, as sent to the host
        uint16_t buttons;
        float filter_hz;
        bool game_mode;
        uint16_t game_keys; // GameMode::active_mask()
    };

    // Read and handle pending commands. Returns a request that main has to act on.
    Request poll(DipoleModel& dipole_model);

    // Send telemetry at a limited rate while streaming is enabled and the port is open.
    void stream(uint32_t now_ms, const Telemetry& telemetry);

    // True while the configurator page is connected and streaming; keeps the mouse awake.
    bool active();
}
