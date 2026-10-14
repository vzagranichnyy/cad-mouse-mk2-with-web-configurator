#include "config.h"

#include "normalization.h"

namespace {
    // Rest pose in normalized units, tracked by auto zero. Written by Core 1 only.
    volatile float g_rest_offset[6] = {0.0f};
    volatile bool g_rest_offset_reset_requested = false; // Set by Core 0, consumed by Core 1

    void clamp_magnitude(volatile float* vector, float max_magnitude)
    {
        const float magnitude = sqrtf(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (magnitude > max_magnitude && magnitude > 1e-8f) {
            const float scale = max_magnitude / magnitude;
            for (int i = 0; i < 3; ++i) {
                vector[i] *= scale;
            }
        }
    }

    void apply_auto_zero(float* pos, float dt, float time_constant_s, float translation_deadzone, float rotation_deadzone)
    {
        // While the knob rests inside the deadzone, slowly follow its rest pose and subtract it.
        // Spring creep, hysteresis after a movement, and calibration residuals then do not leave
        // a bias that wins the start of the next movement. Learning only happens where the output
        // is zero anyway, so an intended movement is never absorbed.
        if (g_rest_offset_reset_requested || time_constant_s <= 0.0f) {
            g_rest_offset_reset_requested = false;
            for (int i = 0; i < 6; ++i) {
                g_rest_offset[i] = 0.0f;
            }
            if (time_constant_s <= 0.0f) {
                return;
            }
        }

        float corrected[6];
        for (int i = 0; i < 6; ++i) {
            corrected[i] = pos[i] - g_rest_offset[i];
        }

        const float translation_magnitude = sqrtf(corrected[0] * corrected[0] + corrected[1] * corrected[1] + corrected[2] * corrected[2]);
        const float rotation_magnitude = sqrtf(corrected[3] * corrected[3] + corrected[4] * corrected[4] + corrected[5] * corrected[5]);

        if (time_constant_s > 0.0f && dt > 0.0f &&
            translation_magnitude < translation_deadzone &&
            rotation_magnitude < rotation_deadzone) {
            const float alpha = fminf(dt / time_constant_s, 1.0f);
            for (int i = 0; i < 6; ++i) {
                g_rest_offset[i] += alpha * corrected[i];
            }

            // Keep the offset within half a deadzone. A slow drag can then never walk it away,
            // and a knob at its true rest stays inside the deadzone relative to the offset,
            // so learning remains active and a wrong offset heals itself.
            clamp_magnitude(&g_rest_offset[0], 0.5f * translation_deadzone);
            clamp_magnitude(&g_rest_offset[3], 0.5f * rotation_deadzone);
        }

        for (int i = 0; i < 6; ++i) {
            pos[i] -= g_rest_offset[i];
        }
    }

    void apply_3d_deadzone_coupled(float* pos, float* vel, float deadzone_threshold, float max_value)
    {
        // Calculate the magnitude of the position vector
        float pos_magnitude = sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);

        if (pos_magnitude < deadzone_threshold) {
            // If within deadzone, zero out both position and velocity
            for (int i = 0; i < 3; ++i) {
                pos[i] = 0.0f;
                vel[i] = 0.0f;
            }
        }
        else {
            // Scale the position and velocity to account for the deadzone
            float scale = (pos_magnitude - deadzone_threshold) / (max_value - deadzone_threshold) / pos_magnitude;
            for (int i = 0; i < 3; ++i) {
                pos[i] *= scale;
                vel[i] *= scale;
            }
        }
    }

    void apply_cross_axis_deadzone(float* state, float relative_threshold)
    {
        // Suppress axes that are weak compared to the dominant one, so a push in one
        // direction does not leak into the others. Axes below relative_threshold * dominant
        // are zeroed; stronger axes shrink linearly, so the output stays continuous and
        // the dominant axis itself is unchanged. Velocities receive the same gain.
        if (relative_threshold <= 0.0f) {
            return;
        }

        float dominant = 0.0f;
        for (int i = 0; i < 6; ++i) {
            dominant = fmaxf(dominant, fabsf(state[i]));
        }
        if (dominant <= 1e-8f) {
            return;
        }

        const float threshold = relative_threshold * dominant;
        for (int i = 0; i < 6; ++i) {
            const float magnitude = fabsf(state[i]);
            float gain = 0.0f;
            if (magnitude > threshold) {
                gain = (magnitude - threshold) / (1.0f - relative_threshold) / magnitude;
            }
            state[i] *= gain;
            state[i + 6] *= gain;
        }
    }

    float apply_power(float x, float power)
    {
        // Shortcuts to harness RP2350 floating point hardware capabilities
        // Limited to plus, minus, multiply, divide, and square root operations.
        if (x <= 1e-8f) {
            return 0.0f;
        }
        if (power == 1.0f) {
            return x;
        }
        else if (power == 2.0f) {
            return x * x;
        }
        else if (power == 3.0f) {
            return x * x * x;
        }
        else if (power == 0.5f) {
            return sqrtf(x);
        }
        else {
            // For other powers, use the standard (slow) library function
            return powf(x, power);
        }
    }
}

void Normalization::request_rest_offset_reset()
{
    g_rest_offset_reset_requested = true;
}

void Normalization::get_rest_offset(float offset[6])
{
    for (int i = 0; i < 6; ++i) {
        offset[i] = g_rest_offset[i];
    }
}

void Normalization::apply_normalization_deadzone_isolation(float state[12], float return_state[12], float dt, const Settings::Values& settings, const Decoupling::Matrix& decoupling, float pose_out[6], float raw_pose_out[6])
{
    // Physical pose before decoupling, for the guided decoupling calibration
    if (raw_pose_out != nullptr) {
        for (int i = 0; i < 6; ++i) {
            raw_pose_out[i] = state[i];
        }
    }

    // == Cross-axis decoupling
    // Remove the measured response of the other axes, in physical units before normalization.
    float decoupled[12];
    for (int i = 0; i < 12; ++i) {
        decoupled[i] = state[i];
    }
    Decoupling::apply(decoupling, &decoupled[0], &decoupled[6]);

    // Per DoF Limits for normalization
    // We allow individual limits, as spring might behave differently for x,y vs z and rx,ry vs rz.
    const float* translation_limits_max = settings.translation_max; // mm
    const float* translation_limits_min = settings.translation_min; // mm
    const float rotation_limits_max[3] = {
      settings.rotation_max[0] * (3.14159265f / 180.0f),
      settings.rotation_max[1] * (3.14159265f / 180.0f),
      settings.rotation_max[2] * (3.14159265f / 180.0f),
    }; // radians
    const float rotation_limits_min[3] = {
      settings.rotation_min[0] * (3.14159265f / 180.0f),
      settings.rotation_min[1] * (3.14159265f / 180.0f),
      settings.rotation_min[2] * (3.14159265f / 180.0f),
    }; // radians

    // == Scale translation and rotation values relative to their configured limits.
    // Values may temporarily exceed [-1, 1]; the isolation step bounds the final pose.
    // Use the same scaling for velocities so deadzone and isolation gains remain coupled.
    // This makes
    // - x,y,z: mm -> effort%
    // - rx,ry,rz: radians -> effort%
    // - vx,vy,vz: mm/s -> effort%/s
    // - vrx,vry,vrz: radians/s -> effort%/s
    float normalized_state[12];
    for (int i = 0; i < 3; ++i) {
        if (decoupled[i] < 0.0f) {
            normalized_state[i] = decoupled[i] / translation_limits_min[i];         // Translation
            normalized_state[i + 6] = decoupled[i + 6] / translation_limits_min[i]; // Translation Velocity
        }
        else {
            normalized_state[i] = decoupled[i] / translation_limits_max[i];         // Translation
            normalized_state[i + 6] = decoupled[i + 6] / translation_limits_max[i]; // Translation Velocity
        }
        if (decoupled[i + 3] < 0.0f) {
            normalized_state[i + 3] = decoupled[i + 3] / rotation_limits_min[i]; // Rotation
            normalized_state[i + 9] = decoupled[i + 9] / rotation_limits_min[i]; // Rotation Velocity
        }
        else {
            normalized_state[i + 3] = decoupled[i + 3] / rotation_limits_max[i]; // Rotation
            normalized_state[i + 9] = decoupled[i + 9] / rotation_limits_max[i]; // Rotation Velocity
        }
    }

    // == Auto zero
    // Remove the slowly tracked rest pose before any deadzone is evaluated.
    apply_auto_zero(normalized_state, dt, settings.auto_zero_time_constant_s, settings.deadzone_translation, settings.deadzone_rotation);

    // Normalized pose before deadzones, for the configurator live view
    if (pose_out != nullptr) {
        for (int i = 0; i < 6; ++i) {
            pose_out[i] = normalized_state[i];
        }
    }

    // == Deadzone
    // Treat translation and rotation separately, as they may have different deadzone thresholds.
    // Use magnitude of translation and rotation vectors to determine if they are within the deadzone.
    // Velocities are also zeroed out if the corresponding translation or rotation is within the deadzone, to prevent drift.
    const float translation_deadzone_threshold = settings.deadzone_translation;
    const float rotation_deadzone_threshold = settings.deadzone_rotation;

    apply_3d_deadzone_coupled(&normalized_state[0], &normalized_state[6], translation_deadzone_threshold, 1.0f);
    apply_3d_deadzone_coupled(&normalized_state[3], &normalized_state[9], rotation_deadzone_threshold, 1.0f);

    // == Cross-axis deadzone
    // Ignore axes that are much weaker than the dominant one across all 6 DoF,
    // e.g. the slight tilt that comes with a sideways push.
    apply_cross_axis_deadzone(normalized_state, settings.cross_axis_ratio);

    // == 6DoF Dominance Isolation
    // The idea is to apply power of X (2 or 3) to all 6 DoFs, and then normalize them back to [-1, 1] range.
    // This ensures that if one DoF is dominant, it will be preserved, while the others will be suppressed.
    float mag_6dof = 0.0f;
    for (int i = 0; i < 6; ++i) {
        mag_6dof += normalized_state[i] * normalized_state[i];
    }
    mag_6dof = sqrt(mag_6dof); // Magnitude of the 6DoF vector

    // Bail early if the magnitude is zero to avoid division by zero
    if (mag_6dof <= 1e-8f) {
        for (int i = 0; i < 12; ++i) {
            return_state[i] = 0.0f;
        }
        return;
    }

    // Handle power curve for isolation.
    // WARNING: Only values of 1, 2, 3, and 0.5 are optimized for RP2350 hardware. Other values will be slow.
    const float power = settings.isolation_power;

    float clamped_mag_6dof = (mag_6dof > 1.0f) ? 1.0f : mag_6dof; // Clamp to [0, 1] range
    float curved_mag_6dof = apply_power(clamped_mag_6dof, power);

    float snapped_mag = 0.0f;
    float snapped_effort[6] = {0.0f};

    for (int i = 0; i < 6; ++i) {
        float dir = normalized_state[i] / mag_6dof; // Direction of the DoF
        float sign = (dir >= 0.0f) ? 1.0f : -1.0f;

        snapped_effort[i] = sign * apply_power(fabsf(dir), power);
        snapped_mag += snapped_effort[i] * snapped_effort[i];
    }
    snapped_mag = sqrtf(snapped_mag); // Magnitude of the snapped effort vector

    // Reconstruct the gain for each DoF
    for (int i = 0; i < 6; ++i) {
        float final_pos = (snapped_effort[i] / snapped_mag) * curved_mag_6dof; // Scale by the curved magnitude

        // Determin the gain to apply to the velocity as well, based on the position gain.
        float axis_gain = (fabsf(normalized_state[i]) > 1e-8f) ? (final_pos / normalized_state[i]) : 0.0f;
        return_state[i] = final_pos; // Update the position
        return_state[i + 6] = normalized_state[i + 6] * axis_gain;
    }
}