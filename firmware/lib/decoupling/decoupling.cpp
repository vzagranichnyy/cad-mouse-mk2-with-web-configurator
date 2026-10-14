#include "decoupling.h"

#include <math.h>

#include "seqlock_value.h"
#include "storage.h"

namespace Decoupling {
    namespace {
        const char* const DECOUPLING_PATH = "/decoupling.bin";
        const uint32_t DECOUPLING_MAGIC = 0x44434D43u; // "CMCD"
        const uint16_t DECOUPLING_VERSION = 1;

        Matrix g_matrix = identity();
        SeqlockValue<Matrix> g_shared;

        void multiply(const float m[6][6], float vector[6])
        {
            float result[6];
            for (int row = 0; row < 6; ++row) {
                float sum = 0.0f;
                for (int col = 0; col < 6; ++col) {
                    sum += m[row][col] * vector[col];
                }
                result[row] = sum;
            }
            for (int i = 0; i < 6; ++i) {
                vector[i] = result[i];
            }
        }

        bool is_valid(const Matrix& matrix)
        {
            for (int row = 0; row < 6; ++row) {
                for (int col = 0; col < 6; ++col) {
                    if (!isfinite(matrix.m[row][col])) {
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    Matrix identity()
    {
        Matrix matrix;
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 6; ++col) {
                matrix.m[row][col] = row == col ? 1.0f : 0.0f;
            }
        }
        matrix.enabled = 0;
        return matrix;
    }

    void begin()
    {
        Matrix matrix;
        if (!load(matrix)) {
            matrix = identity();
        }
        publish(matrix);
    }

    const Matrix& get()
    {
        return g_matrix;
    }

    void publish(const Matrix& matrix)
    {
        g_matrix = matrix;
        if (!is_valid(g_matrix)) {
            g_matrix = identity();
        }
        g_shared.publish(g_matrix);
    }

    bool fetch(uint32_t& last_sequence, Matrix& out)
    {
        return g_shared.fetch(last_sequence, out);
    }

    void apply(const Matrix& matrix, float pose[6], float velocity[6])
    {
        if (!matrix.enabled) {
            return;
        }
        multiply(matrix.m, pose);
        multiply(matrix.m, velocity);
    }

    void print_json(Print& out)
    {
        out.print("{\"t\":\"dc\",\"on\":");
        out.print(g_matrix.enabled ? 1 : 0);
        out.print(",\"m\":[");
        for (int row = 0; row < 6; ++row) {
            for (int col = 0; col < 6; ++col) {
                if (row > 0 || col > 0) {
                    out.print(',');
                }
                out.print(g_matrix.m[row][col], 5);
            }
        }
        out.println("]}");
    }

    bool save()
    {
        return Storage::save(DECOUPLING_PATH, DECOUPLING_MAGIC, DECOUPLING_VERSION, &g_matrix, sizeof(Matrix));
    }

    bool load(Matrix& out)
    {
        Matrix matrix;
        if (!Storage::load(DECOUPLING_PATH, DECOUPLING_MAGIC, DECOUPLING_VERSION, &matrix, sizeof(Matrix)) || !is_valid(matrix)) {
            return false;
        }
        out = matrix;
        return true;
    }
} // namespace Decoupling
