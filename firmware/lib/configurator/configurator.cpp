#include "configurator.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "config_drive.h"
#include "decoupling.h"
#include "normalization.h"
#include "settings.h"

namespace Configurator {
    namespace {
        const uint32_t TELEMETRY_INTERVAL_MS = 33; // ~30 Hz
        const int TELEMETRY_MIN_FREE_BYTES = 200;  // skip a frame instead of blocking when the host is not reading
        const size_t LINE_MAX = 512; // dc_set carries 36 numbers

        char g_line[LINE_MAX];
        size_t g_line_length = 0;
        int g_stream_mode = 0; // 0 = off, 1 = telemetry, 2 = telemetry with the physical pose
        uint32_t g_last_telemetry_ms = 0;

        void print_ok(const char* command)
        {
            Serial.printf("{\"t\":\"ok\",\"cmd\":\"%s\"}\n", command);
        }

        void print_error(const char* message)
        {
            Serial.printf("{\"t\":\"err\",\"msg\":\"%s\"}\n", message);
        }

        // Scaled integers keep the telemetry cheap to format on the RP2040.
        void print_scaled_array(const char* key, const float* values, int count, float scale)
        {
            Serial.print(",\"");
            Serial.print(key);
            Serial.print("\":[");
            for (int i = 0; i < count; ++i) {
                if (i > 0) {
                    Serial.print(',');
                }
                Serial.print(static_cast<long>(lroundf(values[i] * scale)));
            }
            Serial.print(']');
        }

        void handle_decoupling_set()
        {
            Decoupling::Matrix matrix = Decoupling::get();
            for (int i = 0; i < 36; ++i) {
                char* value_text = strtok(nullptr, " \t");
                char* end = nullptr;
                const float value = value_text != nullptr ? strtof(value_text, &end) : 0.0f;
                if (value_text == nullptr || end == value_text || !isfinite(value)) {
                    print_error("usage: dc_set <36 values>");
                    return;
                }
                matrix.m[i / 6][i % 6] = value;
            }
            Decoupling::publish(matrix);
            Decoupling::print_json(Serial);
        }

        void print_info(DipoleModel& dipole_model)
        {
            float moments[3];
            float offsets[6];
            dipole_model.get_magnetic_moments(moments);
            dipole_model.get_offsets(offsets);

            Serial.print("{\"t\":\"info\",\"fw\":\"" CONFIGURATOR_FIRMWARE_VERSION "\",\"cpu_mhz\":");
            Serial.print(static_cast<unsigned long>(F_CPU / 1000000UL));
            Serial.print(",\"drive\":");
            Serial.print(ConfigDrive::active() ? 1 : 0);
            Serial.print(",\"moments\":[");
            for (int i = 0; i < 3; ++i) {
                if (i > 0) {
                    Serial.print(',');
                }
                Serial.print(moments[i], 5);
            }
            // Offsets: x, y, z in mm, rx, ry, rz converted to degrees
            Serial.print("],\"offsets\":[");
            for (int i = 0; i < 6; ++i) {
                if (i > 0) {
                    Serial.print(',');
                }
                Serial.print(i < 3 ? offsets[i] : offsets[i] * (180.0f / 3.14159265f), 4);
            }
            Serial.println("]}");
        }

        Request handle_line(char* line, DipoleModel& dipole_model)
        {
            char* command = strtok(line, " \t");
            if (command == nullptr) {
                return Request::NONE;
            }

            if (strcmp(command, "get") == 0) {
                Settings::print_json(Serial);
            }
            else if (strcmp(command, "set") == 0) {
                char* key = strtok(nullptr, " \t");
                char* value_text = strtok(nullptr, " \t");
                char* end = nullptr;
                const float value = value_text != nullptr ? strtof(value_text, &end) : 0.0f;
                if (key == nullptr || value_text == nullptr || end == value_text) {
                    print_error("usage: set <key> <value>");
                }
                else if (!Settings::set(key, value)) {
                    print_error("unknown key");
                }
                else {
                    Settings::print_json(Serial);
                }
            }
            else if (strcmp(command, "save") == 0) {
                if (Settings::save()) {
                    print_ok("save");
                }
                else {
                    print_error("save failed");
                }
            }
            else if (strcmp(command, "defaults") == 0) {
                Settings::publish(Settings::defaults());
                Settings::print_json(Serial);
            }
            else if (strcmp(command, "reload") == 0) {
                Settings::Values values;
                if (Settings::load(values)) {
                    Settings::publish(values);
                    Settings::print_json(Serial);
                }
                else {
                    print_error("no saved settings");
                }
            }
            else if (strcmp(command, "dc_get") == 0) {
                Decoupling::print_json(Serial);
            }
            else if (strcmp(command, "dc_set") == 0) {
                handle_decoupling_set();
            }
            else if (strcmp(command, "dc_on") == 0) {
                char* argument = strtok(nullptr, " \t");
                Decoupling::Matrix matrix = Decoupling::get();
                matrix.enabled = argument != nullptr && atoi(argument) != 0 ? 1u : 0u;
                Decoupling::publish(matrix);
                Decoupling::print_json(Serial);
            }
            else if (strcmp(command, "dc_save") == 0) {
                if (Decoupling::save()) {
                    print_ok("dc_save");
                }
                else {
                    print_error("dc_save failed");
                }
            }
            else if (strcmp(command, "dc_reload") == 0) {
                Decoupling::Matrix matrix;
                if (Decoupling::load(matrix)) {
                    Decoupling::publish(matrix);
                    Decoupling::print_json(Serial);
                }
                else {
                    print_error("no saved decoupling");
                }
            }
            else if (strcmp(command, "dc_reset") == 0) {
                Decoupling::publish(Decoupling::identity());
                Decoupling::print_json(Serial);
            }
            else if (strcmp(command, "info") == 0) {
                print_info(dipole_model);
            }
            else if (strcmp(command, "stream") == 0) {
                char* argument = strtok(nullptr, " \t");
                g_stream_mode = argument != nullptr ? atoi(argument) : 0;
                print_ok("stream");
            }
            else if (strcmp(command, "calibrate") == 0) {
                print_ok("calibrate");
                return Request::CALIBRATE;
            }
            else if (strcmp(command, "drive") == 0) {
                print_ok("drive");
                Serial.flush();
                delay(50);
                ConfigDrive::reboot_into_drive();
            }
            else if (strcmp(command, "reboot") == 0) {
                print_ok("reboot");
                Serial.flush();
                delay(50);
                rp2040.reboot();
            }
            else {
                print_error("unknown command");
            }
            return Request::NONE;
        }
    } // namespace

    Request poll(DipoleModel& dipole_model)
    {
        Request request = Request::NONE;

        while (Serial.available() > 0) {
            const int c = Serial.read();
            if (c < 0) {
                break;
            }
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                g_line[g_line_length] = '\0';
                const Request line_request = handle_line(g_line, dipole_model);
                if (line_request != Request::NONE) {
                    request = line_request;
                }
                g_line_length = 0;
                continue;
            }
            if (g_line_length < LINE_MAX - 1) {
                g_line[g_line_length++] = static_cast<char>(c);
            }
        }

        return request;
    }

    void stream(uint32_t now_ms, const Telemetry& telemetry)
    {
        if (g_stream_mode == 0) {
            return;
        }
        if (!Serial) {
            g_stream_mode = 0; // Page closed the port; it sends "stream 1" again on reconnect
            return;
        }
        if (now_ms - g_last_telemetry_ms < TELEMETRY_INTERVAL_MS) {
            return;
        }
        if (Serial.availableForWrite() < TELEMETRY_MIN_FREE_BYTES) {
            return;
        }
        g_last_telemetry_ms = now_ms;

        float rest_offset[6];
        Normalization::get_rest_offset(rest_offset);

        Serial.print("{\"t\":\"tel\",\"st\":");
        Serial.print(telemetry.state);
        Serial.print(",\"cal\":");
        Serial.print(telemetry.calibration_state);
        Serial.print(",\"b\":");
        Serial.print(telemetry.buttons);
        Serial.print(",\"hz\":");
        Serial.print(static_cast<long>(lroundf(telemetry.filter_hz)));
        print_scaled_array("p", telemetry.pose, 6, 1000.0f);
        print_scaled_array("o", telemetry.output, 6, 1000.0f);
        print_scaled_array("az", rest_offset, 6, 1000.0f);
        if (g_stream_mode == 2) {
            print_scaled_array("r", telemetry.raw_pose, 6, 10000.0f); // 0.1 um and 0.1 mrad
        }
        Serial.println('}');
    }

    bool active()
    {
        return g_stream_mode != 0 && Serial;
    }
} // namespace Configurator
