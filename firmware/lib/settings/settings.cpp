#include "settings.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "config.h"
#include "seqlock_value.h"
#include "storage.h"

namespace Settings {
    namespace {
        const char* const SETTINGS_PATH = "/settings.bin";
        const uint32_t SETTINGS_MAGIC = 0x53434D43u; // "CMCS"
        const uint16_t SETTINGS_VERSION = 2; // 2: auto zero off by default, drop files saved with it on

        struct Field {
            const char* key;
            size_t offset;
            float min;
            float max;
        };

#define SETTINGS_FIELD(key, member, min, max) {key, offsetof(Values, member), min, max}
        // Keys and ranges must match tools/configurator.html.
        const Field FIELDS[] = {
          SETTINGS_FIELD("dz_t", deadzone_translation, 0.0f, 0.5f),
          SETTINGS_FIELD("dz_r", deadzone_rotation, 0.0f, 0.5f),
          SETTINGS_FIELD("cross", cross_axis_ratio, 0.0f, 0.9f),
          SETTINGS_FIELD("iso", isolation_power, 0.5f, 5.0f),
          SETTINGS_FIELD("az", auto_zero_time_constant_s, 0.0f, 10.0f),
          SETTINGS_FIELD("x_max", translation_max[0], 0.2f, 5.0f),
          SETTINGS_FIELD("x_min", translation_min[0], 0.2f, 5.0f),
          SETTINGS_FIELD("y_max", translation_max[1], 0.2f, 5.0f),
          SETTINGS_FIELD("y_min", translation_min[1], 0.2f, 5.0f),
          SETTINGS_FIELD("z_max", translation_max[2], 0.2f, 5.0f),
          SETTINGS_FIELD("z_min", translation_min[2], 0.2f, 5.0f),
          SETTINGS_FIELD("rx_max", rotation_max[0], 0.5f, 20.0f),
          SETTINGS_FIELD("rx_min", rotation_min[0], 0.5f, 20.0f),
          SETTINGS_FIELD("ry_max", rotation_max[1], 0.5f, 20.0f),
          SETTINGS_FIELD("ry_min", rotation_min[1], 0.5f, 20.0f),
          SETTINGS_FIELD("rz_max", rotation_max[2], 0.5f, 20.0f),
          SETTINGS_FIELD("rz_min", rotation_min[2], 0.5f, 20.0f),
          SETTINGS_FIELD("ekf_q", ekf_process_noise_std, 0.05f, 20.0f),
          SETTINGS_FIELD("ekf_r", ekf_sensor_noise_std, 0.2f, 50.0f),
          SETTINGS_FIELD("sleep", sleep_timeout_s, 0.0f, 3600.0f),
        };
#undef SETTINGS_FIELD

        const size_t FIELD_COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);
        static_assert(sizeof(Values) == FIELD_COUNT * sizeof(float), "Every value needs a configurator field");

        Values g_values = defaults();
        SeqlockValue<Values> g_shared;

        float* field_ptr(Values& values, const Field& field)
        {
            return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&values) + field.offset);
        }

        float clamp_field(const Field& field, float value)
        {
            // fmaxf() returns the other operand for NaN, so NaN ends up at field.min.
            return fminf(fmaxf(value, field.min), field.max);
        }

        void clamp_all(Values& values)
        {
            for (size_t i = 0; i < FIELD_COUNT; ++i) {
                float* value = field_ptr(values, FIELDS[i]);
                *value = clamp_field(FIELDS[i], *value);
            }
        }
    } // namespace

    Values defaults()
    {
        Values values;
        values.deadzone_translation = DEADZONE_TRANSLATION_THRESHOLD;
        values.deadzone_rotation = DEADZONE_ROTATION_THRESHOLD;
        values.cross_axis_ratio = DEADZONE_CROSS_AXIS_RATIO;
        values.isolation_power = ISOLATION_POWER;
        values.auto_zero_time_constant_s = AUTO_ZERO_TIME_CONSTANT_S;
        values.translation_max[0] = NORMALIZATION_X_MAX;
        values.translation_max[1] = NORMALIZATION_Y_MAX;
        values.translation_max[2] = NORMALIZATION_Z_MAX;
        values.translation_min[0] = NORMALIZATION_X_MIN;
        values.translation_min[1] = NORMALIZATION_Y_MIN;
        values.translation_min[2] = NORMALIZATION_Z_MIN;
        values.rotation_max[0] = NORMALIZATION_RX_MAX;
        values.rotation_max[1] = NORMALIZATION_RY_MAX;
        values.rotation_max[2] = NORMALIZATION_RZ_MAX;
        values.rotation_min[0] = NORMALIZATION_RX_MIN;
        values.rotation_min[1] = NORMALIZATION_RY_MIN;
        values.rotation_min[2] = NORMALIZATION_RZ_MIN;
        values.ekf_process_noise_std = EKF_PROCESS_NOISE_STD;
        values.ekf_sensor_noise_std = EKF_SENSOR_NOISE_STD;
        values.sleep_timeout_s = RUNNING_STATE_INACTIVITY_TIMEOUT_MS / 1000.0f;
        return values;
    }

    void begin()
    {
        Values values;
        if (!load(values)) {
            values = defaults();
        }
        publish(values);
    }

    const Values& get()
    {
        return g_values;
    }

    void publish(const Values& values)
    {
        g_values = values;
        g_shared.publish(values);
    }

    bool fetch(uint32_t& last_sequence, Values& out)
    {
        return g_shared.fetch(last_sequence, out);
    }

    bool set(const char* key, float value)
    {
        for (size_t i = 0; i < FIELD_COUNT; ++i) {
            if (strcmp(key, FIELDS[i].key) == 0) {
                Values values = g_values;
                *field_ptr(values, FIELDS[i]) = clamp_field(FIELDS[i], value);
                publish(values);
                return true;
            }
        }
        return false;
    }

    void print_json(Print& out)
    {
        out.print("{\"t\":\"settings\",\"v\":{");
        for (size_t i = 0; i < FIELD_COUNT; ++i) {
            if (i > 0) {
                out.print(',');
            }
            out.print('"');
            out.print(FIELDS[i].key);
            out.print("\":");
            out.print(*field_ptr(g_values, FIELDS[i]), 4);
        }
        out.println("}}");
    }

    bool save()
    {
        return Storage::save(SETTINGS_PATH, SETTINGS_MAGIC, SETTINGS_VERSION, &g_values, sizeof(Values));
    }

    bool load(Values& out)
    {
        Values values;
        if (!Storage::load(SETTINGS_PATH, SETTINGS_MAGIC, SETTINGS_VERSION, &values, sizeof(Values))) {
            return false;
        }

        clamp_all(values);
        out = values;
        return true;
    }
} // namespace Settings
