#pragma once

#include <Arduino.h>

#include "decoupling.h"
#include "settings.h"

namespace Normalization {
    // dt in seconds drives auto zero; pass 0 to skip learning for this sample.
    // For the configurator (both optional): pose_out receives the normalized pose after
    // decoupling and auto zero but before deadzones, raw_pose_out the physical pose
    // (mm, rad) before decoupling.
    void apply_normalization_deadzone_isolation(float state[12], float return_state[12], float dt, const Settings::Values& settings, const Decoupling::Matrix& decoupling, float pose_out[6], float raw_pose_out[6]);

    // Rest pose currently removed by auto zero, in normalized units (may be read from Core 0).
    void get_rest_offset(float offset[6]);

    // Forget the auto zero rest pose, e.g. after a new calibration (may be called from Core 0).
    void request_rest_offset_reset();
}
