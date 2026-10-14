#pragma once

#include <Arduino.h>

#include "dipole_model.h"

class ExtendedKalmanFilter {
public:
    ExtendedKalmanFilter();
    void init(const float initial_state[6], float process_noise_std, float sensor_noise_std);
    void set_noise(float process_noise_std, float sensor_noise_std);
    void predict(float dt);
    void update(float sensor_readings[9], DipoleModel& dipole_model);
    void get_state(float state_out[12]);

private:
    // 12 state variables: [x, y, z, rx, ry, rz, vx, vy, vz, vrx, vry, vrz]
    float m_x[12];

    // 12x12 state covariance matrix
    float m_P[12][12];

    // Process noise covariance matrix spectral density (assumed to be diagonal)
    float m_Q_pos, m_Q_rot, m_Q_vel, m_Q_ang_vel;

    // Sensor noise covariance matrix spectral density
    float m_R_var;

    // Jacobian workspace (recomputed each update step).
    float m_cached_H[9][12];

    // Helper function for Jacobian calculation
    void compute_jacobian(const float state[12], float H[9][12], DipoleModel& dipole_model, float base_readings_out[9] = nullptr);
};