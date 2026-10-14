#pragma once

#include <Arduino.h>

namespace CoreMailbox {

    struct RawSensorData {
        float values[9];
        uint32_t timestamp_us;
    };

    struct FilteredData {
        float state[12];
        float pose[6];     // normalized pose before deadzones, for the configurator
        float raw_pose[6]; // physical pose before decoupling (mm, rad), for the configurator
        float dt;
    };

    void setCore1Sleeping(bool sleeping);
    bool core1Sleeping();

    void publishRaw(const float (&values)[9], uint32_t timestamp_us);
    bool consumeRaw(uint32_t& last_sequence, RawSensorData& data);

    void publishFiltered(const float (&state)[12], const float (&pose)[6], const float (&raw_pose)[6], float dt);
    bool consumeFiltered(uint32_t& last_sequence, FilteredData& data);

} // namespace CoreMailbox