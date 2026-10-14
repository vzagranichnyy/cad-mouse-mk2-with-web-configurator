#include "calibration.h"

#include "dipole_model.h"
#include <LittleFS.h>

#if DEBUG_CALIBRATION_SERIAL
#define CAL_LOG_PRINT(...) Serial.print(__VA_ARGS__)
#define CAL_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define CAL_LOG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define CAL_LOG_PRINT(...)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             \
    do {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    } while (0)
#define CAL_LOG_PRINTLN(...)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
    do {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    } while (0)
#define CAL_LOG_PRINTF(...)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            \
    do {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    } while (0)
#endif

static struct {
    float sum[9] = {0};
    float sq_sum[9] = {0};
    uint8_t count = 0;
} stats;

void Calibration::reset_calibration_data()
{
    for (int i = 0; i < 9; ++i) {
        stats.sum[i] = 0.0f;
        stats.sq_sum[i] = 0.0f;
    }
    stats.count = 0;
}

void Calibration::add_sample(float raw_data[9])
{
    for (int i = 0; i < 9; ++i) {
        stats.sum[i] += raw_data[i];
        stats.sq_sum[i] += raw_data[i] * raw_data[i];
    }
    stats.count++;
}

uint8_t Calibration::get_sample_count()
{
    return stats.count;
}

void Calibration::get_current_means(float means[9])
{
    if (stats.count == 0) {
        for (int i = 0; i < 9; ++i) {
            means[i] = 0.0f;
        }
        return;
    }

    for (int i = 0; i < 9; ++i) {
        means[i] = stats.sum[i] / stats.count;
    }
}

void Calibration::get_current_stds(float stds[9])
{
    if (stats.count == 0) {
        for (int i = 0; i < 9; ++i) {
            stds[i] = 0.0f;
        }
        return;
    }

    for (int i = 0; i < 9; ++i) {
        float mean = stats.sum[i] / stats.count;
        float variance = (stats.sq_sum[i] / stats.count) - (mean * mean);
        stds[i] = sqrtf(variance);
    }
}

void Calibration::get_model_func_moments(float* params, float* readings, DipoleModel& model)
{
    model.set_magnetic_moments(params);
    model.get_expected_readings(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, readings);
}

void Calibration::get_model_func_offsets(float* params, float* readings, DipoleModel& model)
{
    model.set_offsets(params);
    model.get_expected_readings(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, readings);
}

// Helper: Gaussian Elimination with Partial Pivoting (6x6)
// Solves A * x = b for x, where N is the size (up to 6)
void Calibration::solve_linear_system(float A[6][6], float* b, float* x, int N)
{
    for (int i = 0; i < N; i++) {
        // Pivot
        int pivot = i;
        for (int j = i + 1; j < N; j++)
            if (fabs(A[j][i]) > fabs(A[pivot][i]))
                pivot = j;

        for (int k = 0; k < N; k++) {
            float tmp = A[i][k];
            A[i][k] = A[pivot][k];
            A[pivot][k] = tmp;
        }
        float tmp = b[i];
        b[i] = b[pivot];
        b[pivot] = tmp;

        // Eliminate
        for (int j = i + 1; j < N; j++) {
            float factor = A[j][i] / A[i][i];
            for (int k = i; k < N; k++)
                A[j][k] -= factor * A[i][k];
            b[j] -= factor * b[i];
        }
    }
    // Back substitute
    for (int i = N - 1; i >= 0; i--) {
        float sum = 0;
        for (int j = i + 1; j < N; j++)
            sum += A[i][j] * x[j];
        x[i] = (b[i] - sum) / A[i][i];
    }
}

void Calibration::solve_least_squares(float* params, int N, const float* target_readings, DipoleModel& model, void (*get_model_func)(float*, float*, DipoleModel&), const float* min_bounds, const float* max_bounds)
{
    const int MAX_ITER = 15;
    const float lambda = 0.01f; // Damping
    const float eps = 0.002f;   // Numerical Jacobian step

    for (int iter = 0; iter < MAX_ITER; iter++) {
        float current_B[9];
        get_model_func(params, current_B, model);

        float r[9]; // Residuals
        for (int i = 0; i < 9; i++)
            r[i] = target_readings[i] - current_B[i];

        // Calculate and print SSR
#if DEBUG_CALIBRATION_SERIAL
        float total_res = 0;
        for (int i = 0; i < 9; i++)
            total_res += (r[i] * r[i]);
        CAL_LOG_PRINTF("Iter %d, Total Error: %.4f\n", iter, total_res);
#endif

        // 1. Calculate Jacobian (9xN)
        float J[9][6];
        for (int j = 0; j < N; j++) {
            float p_orig = params[j];
            params[j] += eps;
            float B_plus[9];
            get_model_func(params, B_plus, model);
            params[j] = p_orig;
            for (int i = 0; i < 9; i++)
                J[i][j] = (B_plus[i] - current_B[i]) / eps;
        }

        // 2. Form normal equations: (J^T * J + lambda * I) * delta = J^T * r
        float A[6][6] = {0}, b[6] = {0};
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                for (int k = 0; k < 9; k++)
                    A[i][j] += J[k][i] * J[k][j];
                if (i == j)
                    A[i][j] += lambda;
            }
            for (int k = 0; k < 9; k++)
                b[i] += J[k][i] * r[k];
        }

        // 3. Solve for delta
        float delta[6];
        solve_linear_system(A, b, delta, N);

        // 4. Update and clamp using individual bounds
        for (int i = 0; i < N; i++) {
            params[i] += delta[i];
            // Individual bounds constraint
            if (params[i] < min_bounds[i])
                params[i] = min_bounds[i];
            else if (params[i] > max_bounds[i])
                params[i] = max_bounds[i];
        }
    }
}

bool Calibration::compute_calibration(float magnetic_moments[3], float offsets[6], DipoleModel& dipoleModel)
{
    // Fail early if no/insufficient samples
    if (stats.count < 10) {
        Serial.println("Insufficient samples for calibration");
        return false;
    }

    // Calculate means of the collected samples
    float means[9];
    get_current_means(means);

    // Fail early if standard deviation for any sensor is too high, indicating unstable readings
    float stds[9];
    get_current_stds(stds);
    bool high_std_detected = false;
    for (int i = 0; i < 9; ++i) {
        if (stds[i] > CALIBRATION_DATA_STD_THRESHOLD) {
            high_std_detected = true;
        }
    }

    if (high_std_detected) {
        Serial.println("High standard deviation detected in sensor readings, calibration aborted");
        Serial.print("Standard deviations: ");
        for (int i = 0; i < 9; ++i) {
            Serial.print(stds[i], 3);
            if (i < 8)
                Serial.print(", ");
        }
        Serial.println();
        return false;
    }

    // Get current magnetic moments
    float old_magnetic_moments[3];
    dipoleModel.get_magnetic_moments(old_magnetic_moments);

    // Get current offsets
    float old_offsets[6];
    dipoleModel.get_offsets(old_offsets);

    // Create new DipoleModel instance for calibration to avoid modifying the original during optimization
    DipoleModel calibrationModel;
    calibrationModel.set_magnetic_moments(old_magnetic_moments);
    // Note: Not setting offsets yet, assuming all-zero for first stage of calibration.

#if DEBUG_CALIBRATION_SERIAL
    CAL_LOG_PRINT("Old magnetic moments: ");
    for (int i = 0; i < 3; ++i) {
        CAL_LOG_PRINT(old_magnetic_moments[i], 6);
        if (i < 2)
            CAL_LOG_PRINT(", ");
    }
    CAL_LOG_PRINTLN();
#endif

    // Bounded least squares optimization to refine magnetic_moments
    float min_bounds[3] = {-CALIBRATION_FIT_MOMENT_BOUNDS, -CALIBRATION_FIT_MOMENT_BOUNDS, -CALIBRATION_FIT_MOMENT_BOUNDS}; // Lower bounds for magnetic moments
    float max_bounds[3] = {CALIBRATION_FIT_MOMENT_BOUNDS, CALIBRATION_FIT_MOMENT_BOUNDS, CALIBRATION_FIT_MOMENT_BOUNDS};    // Upper bounds for magnetic moments
    float fitted_magnetic_moments[3] = {old_magnetic_moments[0], old_magnetic_moments[1], old_magnetic_moments[2]};
    solve_least_squares(fitted_magnetic_moments, 3, means, calibrationModel, get_model_func_moments, min_bounds, max_bounds);

#if DEBUG_CALIBRATION_SERIAL
    CAL_LOG_PRINT("Fitted magnetic moments: ");
    for (int i = 0; i < 3; ++i) {
        CAL_LOG_PRINT(fitted_magnetic_moments[i], 6);
        if (i < 2)
            CAL_LOG_PRINT(", ");
    }
    CAL_LOG_PRINTLN();
#endif

    // Set the optimized magnetic moments to calibrationModel.
    calibrationModel.set_magnetic_moments(fitted_magnetic_moments);

    // Fit offsets using the optimized magnetic moments
    float min_bounds_offsets[6] = {
      CALIBRATION_FIT_X_MIN,
      CALIBRATION_FIT_Y_MIN,
      CALIBRATION_FIT_Z_MIN,
      CALIBRATION_FIT_RX_MIN / 180.0f * 3.14159265f,
      CALIBRATION_FIT_RY_MIN / 180.0f * 3.14159265f,
      CALIBRATION_FIT_RZ_MIN / 180.0f * 3.14159265f,
    }; // Lower bounds for offsets
    float max_bounds_offsets[6] = {
      CALIBRATION_FIT_X_MAX,
      CALIBRATION_FIT_Y_MAX,
      CALIBRATION_FIT_Z_MAX,
      CALIBRATION_FIT_RX_MAX / 180.0f * 3.14159265f,
      CALIBRATION_FIT_RY_MAX / 180.0f * 3.14159265f,
      CALIBRATION_FIT_RZ_MAX / 180.0f * 3.14159265f,
    }; // Upper bounds for offsets

    float fitted_offsets[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // Start with zero offsets
    solve_least_squares(fitted_offsets, 6, means, calibrationModel, get_model_func_offsets, min_bounds_offsets, max_bounds_offsets);

#if DEBUG_CALIBRATION_SERIAL
    CAL_LOG_PRINT("Fitted offsets: ");
    for (int i = 0; i < 6; ++i) {
        CAL_LOG_PRINT(fitted_offsets[i], 6);
        if (i < 5)
            CAL_LOG_PRINT(", ");
    }
    CAL_LOG_PRINTLN();
#endif

    /*
    Redo both fits with previous fitted parameters as starting points to refine both magnetic moments and offsets further.
    This iterative refinement can help converge to a better solution, especially if the initial guesses were far from the true values.
    The process can be repeated multiple times, but for simplicity, we will do it twice.
    */

    // Redo the magnetic moments fitting using the fitted offsets to refine the magnetic moments further
    calibrationModel.set_offsets(fitted_offsets);
    solve_least_squares(fitted_magnetic_moments, 3, means, calibrationModel, get_model_func_moments, min_bounds, max_bounds);

#if DEBUG_CALIBRATION_SERIAL
    CAL_LOG_PRINT("Refined magnetic moments after offsets fitting: ");
    for (int i = 0; i < 3; ++i) {
        CAL_LOG_PRINT(fitted_magnetic_moments[i], 6);
        if (i < 2)
            CAL_LOG_PRINT(", ");
    }
    CAL_LOG_PRINTLN();
#endif

    // Error check: If the refined magnetic moments are at bounds, or too small.
    // solve_least_squares() clamps to the bounds, so a value at a bound means the fit wanted to go further.
    bool moments_out_of_bounds[3] = {false, false, false};
    for (int i = 0; i < 3; ++i) {
        if (fitted_magnetic_moments[i] <= min_bounds[i] || fitted_magnetic_moments[i] >= max_bounds[i]) {
            moments_out_of_bounds[i] = true;
        }
    }
    bool moments_too_small[3] = {false, false, false};
    for (int i = 0; i < 3; ++i) {
        if (fabs(fitted_magnetic_moments[i]) < CALIBRATION_FIT_MOMENT_MIN) { // Arbitrary threshold for too small magnetic moment
            moments_too_small[i] = true;
        }
    }

    bool moments_invalid = false;
    for (int i = 0; i < 3; ++i) {
        if (moments_out_of_bounds[i] || moments_too_small[i]) {
            moments_invalid = true;
            break;
        }
    }

    if (moments_invalid) {
        Serial.println("Magnetic moments are invalid: At bounds: ");
        for (int i = 0; i < 3; ++i) {
            Serial.print(moments_out_of_bounds[i] ? "1" : "0");
            if (i < 2)
                Serial.print(", ");
        }
        Serial.print(" | Too small: ");
        for (int i = 0; i < 3; ++i) {
            Serial.print(moments_too_small[i] ? "1" : "0");
            if (i < 2)
                Serial.print(", ");
        }

        Serial.println();
        return false;
    }

    // Redo the offsets fitting using the refined magnetic moments to refine the offsets further
    calibrationModel.set_magnetic_moments(fitted_magnetic_moments);
    solve_least_squares(fitted_offsets, 6, means, calibrationModel, get_model_func_offsets, min_bounds_offsets, max_bounds_offsets);

#if DEBUG_CALIBRATION_SERIAL
    CAL_LOG_PRINT("Refined offsets after magnetic moments fitting: ");
    for (int i = 0; i < 6; ++i) {
        CAL_LOG_PRINT(fitted_offsets[i], 6);
        if (i < 5)
            CAL_LOG_PRINT(", ");
    }
    CAL_LOG_PRINTLN();
#endif

    // Final error check: If the refined offsets are at bounds (clamped by solve_least_squares)
    bool offsets_out_of_bounds[6] = {false, false, false, false, false, false};
    for (int i = 0; i < 6; ++i) {
        if (fitted_offsets[i] <= min_bounds_offsets[i] || fitted_offsets[i] >= max_bounds_offsets[i]) {
            offsets_out_of_bounds[i] = true;
        }
    }

    bool offsets_invalid = false;
    for (int i = 0; i < 6; ++i) {
        if (offsets_out_of_bounds[i]) {
            offsets_invalid = true;
            break;
        }
    }

    if (offsets_invalid) {
        Serial.println("Offsets are invalid: At bounds: ");
        for (int i = 0; i < 6; ++i) {
            Serial.print(offsets_out_of_bounds[i] ? "1" : "0");
            if (i < 5)
                Serial.print(", ");
        }
        Serial.println();
        return false;
    }

    /*
    Return the fitted parameters
    This is done after all checks to ensure that only valid parameters are returned.
    */

    for (int i = 0; i < 3; ++i) {
        magnetic_moments[i] = fitted_magnetic_moments[i];
    }
    for (int i = 0; i < 6; ++i) {
        offsets[i] = fitted_offsets[i];
    }

    return true; // Indicate that calibration was successful
}

bool Calibration::initialize_filesystem()
{
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS, attempting to format...");
        if (LittleFS.format()) {
            CAL_LOG_PRINTLN("LittleFS formatted successfully.");
        }
        else {
            Serial.println("Failed to format LittleFS.");
            return false;
        }
        if (!LittleFS.begin()) {
            Serial.println("Failed to mount LittleFS after formatting");
            return false;
        }
    }
    return true;
}

bool Calibration::save_calibration_data(const CalibrationData& data)
{
    File file = LittleFS.open("/calibration.bin", "w");
    if (!file) {
        Serial.println("Failed to open calibration data file for writing");
        return false;
    }

    size_t written = file.write(reinterpret_cast<const uint8_t*>(&data), sizeof(CalibrationData));

    file.flush();
    file.close();

    if (written != sizeof(CalibrationData)) {
        Serial.println("Failed to write complete calibration data");
        return false;
    }

    return true;
}

bool Calibration::load_calibration_data(CalibrationData& data)
{
    File file = LittleFS.open("/calibration.bin", "r");
    if (!file) {
        Serial.println("Failed to open calibration data file for reading");
        return false;
    }

    size_t read = file.read(reinterpret_cast<uint8_t*>(&data), sizeof(CalibrationData));
    file.close();

    if (read != sizeof(CalibrationData)) {
        Serial.println("Failed to read complete calibration data");
        return false;
    }

    return true;
}

bool Calibration::delete_calibration_data()
{
    if (LittleFS.exists("/calibration.bin")) {
        if (!LittleFS.remove("/calibration.bin")) {
            Serial.println("Failed to delete calibration data file");
            return false;
        }
    }
    return true;
}