#include "nomad_mode.h"

#include <math.h>
#include <string.h>

#include "storage.h"

namespace NomadMode {
    namespace {
        const char* const NOMAD_MODE_PATH = "/nomad_mode.bin";
        const uint32_t NOMAD_MODE_MAGIC = 0x4E4D4D43u; // "CMMN"
        const uint16_t NOMAD_MODE_VERSION = 1;

        const int RECENTER_SPAN_PX = 100;     // keep the cursor this close to the gesture start
        const int MAX_STEP_PX = 24;           // largest move in one report
        const uint32_t GESTURE_END_US = 120000; // end a gesture after this long without input

        // HID keyboard usage IDs and modifier bits
        const uint8_t KEY_Z = 0x1D;
        const uint8_t KEY_SPACE = 0x2C;
        const uint8_t MOD_CTRL = 0x01;

        enum class Phase : uint8_t {
            IDLE,
            MODIFIER_DOWN,   // keyboard: hold the gesture modifier
            BUTTON_DOWN,     // mouse: press the gesture button
            DRAG,            // mouse: move with the button held
            RECENTER_UP,     // mouse: release before moving the cursor back
            RECENTER_RETURN, // mouse: move back without a button, then press again
            END_UP,          // mouse: release at the end of the gesture
            END_MODIFIER_UP, // keyboard: release the modifier
            END_RETURN,      // mouse: move the cursor back to where the gesture started
            TAP_DOWN,        // keyboard: shortcut of a knob button
            TAP_UP,
        };

        Config g_config = defaults();
        bool g_enabled = false;

        Phase g_phase = Phase::IDLE;
        Gesture g_gesture = Gesture::NONE;
        float g_acc_x = 0.0f; // pixels not sent yet
        float g_acc_y = 0.0f;
        float g_acc_wheel = 0.0f;
        int g_offset_x = 0; // cursor offset from where the gesture started
        int g_offset_y = 0;
        uint32_t g_last_update_us = 0;
        uint32_t g_last_input_us = 0;
        int g_pending_tap = -1;
        Report g_report = {};

        int clamp_step(int value)
        {
            return value > MAX_STEP_PX ? MAX_STEP_PX : (value < -MAX_STEP_PX ? -MAX_STEP_PX : value);
        }

        void sanitize(Config& config)
        {
            for (int i = 0; i < 6; ++i) {
                if (config.target[i] >= TARGET_COUNT) {
                    config.target[i] = NONE;
                }
                config.sign[i] = config.sign[i] < 0 ? -1 : 1;
            }
            config.rotate_speed = fminf(fmaxf(config.rotate_speed, 20.0f), 5000.0f);
            config.pan_speed = fminf(fmaxf(config.pan_speed, 20.0f), 5000.0f);
            config.zoom_speed = fminf(fmaxf(config.zoom_speed, 0.5f), 60.0f);
            config.rotate_button &= 0x07;
            config.pan_button &= 0x07;
            config.rotate_modifier &= 0x07;
            config.pan_modifier &= 0x07;
            config.restore_cursor = config.restore_cursor ? 1 : 0;
            memset(config.reserved, 0, sizeof(config.reserved));
        }

        void axis_vectors(const float output[6], float& rotate_h, float& rotate_v, float& pan_h, float& pan_v, float& zoom)
        {
            rotate_h = rotate_v = pan_h = pan_v = zoom = 0.0f;
            for (int axis = 0; axis < 6; ++axis) {
                const float value = output[axis] * g_config.sign[axis];
                switch (g_config.target[axis]) {
                    case ROTATE_H: rotate_h += value; break;
                    case ROTATE_V: rotate_v += value; break;
                    case PAN_H: pan_h += value; break;
                    case PAN_V: pan_v += value; break;
                    case ZOOM: zoom += value; break;
                    default: break;
                }
            }
        }

        uint8_t gesture_button()
        {
            return g_gesture == Gesture::ROTATE ? g_config.rotate_button : g_config.pan_button;
        }

        uint8_t gesture_modifier()
        {
            return g_gesture == Gesture::ROTATE ? g_config.rotate_modifier : g_config.pan_modifier;
        }

        int8_t take_wheel()
        {
            const int notches = static_cast<int>(g_acc_wheel);
            return static_cast<int8_t>(notches > 1 ? 1 : (notches < -1 ? -1 : notches));
        }
    } // namespace

    Config defaults()
    {
        // Nomad Sculpt desktop defaults: view_rotate = right drag, view_pan = middle drag, wheel zoom.
        // Rotation directions were checked in Nomad (a right drag turns the camera, not the model).
        Config config = {};
        config.target[0] = PAN_H;    // x: push right -> model right
        config.target[1] = ZOOM;     // y: push forward -> zoom in
        config.target[2] = PAN_V;    // z: pull up -> model up
        config.target[3] = ROTATE_V; // rx: tilt forward/backward
        config.target[4] = ROTATE_H; // ry: tilt sideways
        config.target[5] = ROTATE_H; // rz: twist
        const int8_t signs[6] = {1, 1, 1, 1, -1, 1};
        memcpy(config.sign, signs, sizeof(signs));
        config.rotate_speed = 600.0f;
        config.pan_speed = 500.0f;
        config.zoom_speed = 8.0f;
        config.rotate_button = 0x02; // right
        config.pan_button = 0x04;    // middle
        config.button_modifier[0] = MOD_CTRL;
        config.button_key[0] = KEY_Z;     // left: undo
        config.button_modifier[1] = MOD_CTRL;
        config.button_key[1] = KEY_SPACE; // right: view_focus
        config.restore_cursor = 1;
        return config;
    }

    void begin()
    {
        Config config;
        if (!load(config)) {
            config = defaults();
        }
        g_config = config;
        g_enabled = false;
    }

    const Config& get()
    {
        return g_config;
    }

    void set(const Config& config)
    {
        g_config = config;
        sanitize(g_config);
    }

    bool save()
    {
        return Storage::save(NOMAD_MODE_PATH, NOMAD_MODE_MAGIC, NOMAD_MODE_VERSION, &g_config, sizeof(Config));
    }

    bool load(Config& out)
    {
        Config config;
        if (!Storage::load(NOMAD_MODE_PATH, NOMAD_MODE_MAGIC, NOMAD_MODE_VERSION, &config, sizeof(Config))) {
            return false;
        }
        sanitize(config);
        out = config;
        return true;
    }

    bool enabled()
    {
        return g_enabled;
    }

    void set_enabled(bool enabled)
    {
        if (enabled && !g_enabled) {
            g_last_update_us = 0;
        }
        g_enabled = enabled;
        g_acc_wheel = 0.0f;
        g_pending_tap = -1;
        // A running gesture is still finished by update(): button released, cursor moved back.
    }

    void update(uint32_t now_us, const float output[6])
    {
        if (!g_enabled && g_phase == Phase::IDLE) {
            return;
        }

        float dt = g_last_update_us == 0 ? 0.0f : (now_us - g_last_update_us) * 1e-6f;
        g_last_update_us = now_us;
        if (dt > 0.05f) {
            dt = 0.05f;
        }

        float rotate_h, rotate_v, pan_h, pan_v, zoom;
        axis_vectors(output, rotate_h, rotate_v, pan_h, pan_v, zoom);
        if (!g_enabled) {
            rotate_h = rotate_v = pan_h = pan_v = zoom = 0.0f; // only finish the running gesture
        }
        const float rotate_magnitude = fabsf(rotate_h) + fabsf(rotate_v);
        const float pan_magnitude = fabsf(pan_h) + fabsf(pan_v);

        g_acc_wheel += zoom * g_config.zoom_speed * dt;
        if (zoom == 0.0f && fabsf(g_acc_wheel) < 1.0f) {
            g_acc_wheel = 0.0f; // no leftover fraction when the knob comes back
        }

        if (g_phase == Phase::IDLE) {
            if (!g_enabled) {
                return;
            }
            if (rotate_magnitude > 0.0f || pan_magnitude > 0.0f) {
                g_gesture = rotate_magnitude >= pan_magnitude ? Gesture::ROTATE : Gesture::PAN;
                g_acc_x = g_acc_y = 0.0f;
                g_offset_x = g_offset_y = 0;
                g_last_input_us = now_us;
                g_phase = gesture_modifier() ? Phase::MODIFIER_DOWN : Phase::BUTTON_DOWN;
            }
            else if (g_pending_tap >= 0) {
                g_phase = Phase::TAP_DOWN;
            }
            return;
        }

        if (g_phase == Phase::DRAG || g_phase == Phase::RECENTER_UP || g_phase == Phase::RECENTER_RETURN || g_phase == Phase::BUTTON_DOWN) {
            const bool rotating = g_gesture == Gesture::ROTATE;
            const float h = rotating ? rotate_h : pan_h;
            const float v = rotating ? rotate_v : pan_v;
            const float speed = rotating ? g_config.rotate_speed : g_config.pan_speed;
            if (h != 0.0f || v != 0.0f) {
                g_last_input_us = now_us;
                g_acc_x += h * speed * dt;
                g_acc_y -= v * speed * dt; // screen y grows downwards
            }
            else if (now_us - g_last_input_us > GESTURE_END_US && g_phase == Phase::DRAG) {
                g_acc_x = g_acc_y = 0.0f;
                g_phase = Phase::END_UP;
            }
        }
    }

    void tap_button(int index)
    {
        if (g_enabled && index >= 0 && index < 2 && g_config.button_key[index] != 0) {
            g_pending_tap = index;
        }
    }

    Report next_report()
    {
        Report report = {};
        report.kind = Report::Kind::NONE;

        switch (g_phase) {
            case Phase::IDLE: {
                const int8_t wheel = take_wheel();
                if (wheel != 0) {
                    report.kind = Report::Kind::MOUSE;
                    report.wheel = wheel;
                }
                break;
            }
            case Phase::MODIFIER_DOWN:
                report.kind = Report::Kind::KEYBOARD;
                report.modifier = gesture_modifier();
                break;
            case Phase::BUTTON_DOWN:
                report.kind = Report::Kind::MOUSE;
                report.buttons = gesture_button();
                break;
            case Phase::DRAG: {
                const int dx = clamp_step(static_cast<int>(g_acc_x));
                const int dy = clamp_step(static_cast<int>(g_acc_y));
                const int8_t wheel = take_wheel();
                if (dx != 0 || dy != 0 || wheel != 0) {
                    report.kind = Report::Kind::MOUSE;
                    report.buttons = gesture_button();
                    report.dx = static_cast<int8_t>(dx);
                    report.dy = static_cast<int8_t>(dy);
                    report.wheel = wheel;
                }
                break;
            }
            case Phase::RECENTER_UP:
            case Phase::END_UP:
                report.kind = Report::Kind::MOUSE;
                break;
            case Phase::RECENTER_RETURN:
            case Phase::END_RETURN:
                report.kind = Report::Kind::MOUSE;
                report.dx = static_cast<int8_t>(clamp_step(-g_offset_x));
                report.dy = static_cast<int8_t>(clamp_step(-g_offset_y));
                break;
            case Phase::END_MODIFIER_UP:
                report.kind = Report::Kind::KEYBOARD;
                break;
            case Phase::TAP_DOWN:
                report.kind = Report::Kind::KEYBOARD;
                report.modifier = g_config.button_modifier[g_pending_tap];
                report.key = g_config.button_key[g_pending_tap];
                break;
            case Phase::TAP_UP:
                report.kind = Report::Kind::KEYBOARD;
                break;
        }

        g_report = report;
        return report;
    }

    void report_sent()
    {
        const Report& sent = g_report;
        if (sent.kind == Report::Kind::MOUSE) {
            g_acc_wheel -= sent.wheel;
        }

        switch (g_phase) {
            case Phase::IDLE:
                break;
            case Phase::MODIFIER_DOWN:
                g_phase = Phase::BUTTON_DOWN;
                break;
            case Phase::BUTTON_DOWN:
                g_phase = Phase::DRAG;
                break;
            case Phase::DRAG:
                g_acc_x -= sent.dx;
                g_acc_y -= sent.dy;
                g_offset_x += sent.dx;
                g_offset_y += sent.dy;
                if (abs(g_offset_x) > RECENTER_SPAN_PX || abs(g_offset_y) > RECENTER_SPAN_PX) {
                    g_phase = Phase::RECENTER_UP;
                }
                break;
            case Phase::RECENTER_UP:
                g_phase = Phase::RECENTER_RETURN;
                break;
            case Phase::RECENTER_RETURN:
            case Phase::END_RETURN:
                g_offset_x += sent.dx;
                g_offset_y += sent.dy;
                if (g_offset_x == 0 && g_offset_y == 0) {
                    g_phase = g_phase == Phase::RECENTER_RETURN ? Phase::BUTTON_DOWN : Phase::IDLE;
                    if (g_phase == Phase::IDLE) {
                        g_gesture = Gesture::NONE;
                    }
                }
                break;
            case Phase::END_UP:
                if (gesture_modifier()) {
                    g_phase = Phase::END_MODIFIER_UP;
                }
                else {
                    g_phase = g_config.restore_cursor ? Phase::END_RETURN : Phase::IDLE;
                    if (g_phase == Phase::IDLE) {
                        g_gesture = Gesture::NONE;
                    }
                }
                break;
            case Phase::END_MODIFIER_UP:
                g_phase = g_config.restore_cursor ? Phase::END_RETURN : Phase::IDLE;
                if (g_phase == Phase::IDLE) {
                    g_gesture = Gesture::NONE;
                }
                break;
            case Phase::TAP_DOWN:
                g_phase = Phase::TAP_UP;
                break;
            case Phase::TAP_UP:
                g_pending_tap = -1;
                g_phase = Phase::IDLE;
                break;
        }
    }

    bool busy()
    {
        return g_phase != Phase::IDLE;
    }

    Gesture gesture()
    {
        return g_gesture;
    }

#ifdef NOMAD_MODE_SELFTEST
    // Runs scripted knob input through the state machine without sending anything over USB.
    void self_test(Print& out)
    {
        const Config saved = g_config;
        g_config = defaults();
        set_enabled(true);
        uint32_t now = 1000000;
        uint8_t last_buttons = 0;

        struct Stats {
            int presses, releases, drag_dx, drag_dy, net_dx, net_dy, wheel, max_offset, keys;
        };
        auto run = [&](const float input[6], int steps, Stats& st) {
            for (int i = 0; i < steps; ++i) {
                now += 5000; // 200 Hz loop
                update(now, input);
                const Report r = next_report();
                if (r.kind == Report::Kind::MOUSE) {
                    if (r.buttons & ~last_buttons) st.presses++;
                    if (last_buttons & ~r.buttons) st.releases++;
                    last_buttons = r.buttons;
                    if (r.buttons) {
                        st.drag_dx += r.dx;
                        st.drag_dy += r.dy;
                    }
                    st.net_dx += r.dx;
                    st.net_dy += r.dy;
                    st.wheel += r.wheel;
                }
                else if (r.kind == Report::Kind::KEYBOARD) {
                    out.printf("  key mod=%u key=0x%02X\n", r.modifier, r.key);
                    st.keys++;
                }
                if (r.kind != Report::Kind::NONE) {
                    report_sent();
                }
                const int offset = abs(g_offset_x) > abs(g_offset_y) ? abs(g_offset_x) : abs(g_offset_y);
                if (offset > st.max_offset) st.max_offset = offset;
            }
        };
        auto print = [&](const char* name, const Stats& st) {
            out.printf("%s: presses %d releases %d drag (%d,%d) net (%d,%d) wheel %d max_offset %d keys %d | phase %d busy %d on %d btn %u\n",
              name, st.presses, st.releases, st.drag_dx, st.drag_dy, st.net_dx, st.net_dy, st.wheel, st.max_offset, st.keys,
              static_cast<int>(g_phase), busy() ? 1 : 0, g_enabled ? 1 : 0, last_buttons);
        };
        const float zero[6] = {0, 0, 0, 0, 0, 0};

        Stats st = {};
        const float twist[6] = {0, 0, 0, 0, 0, -0.5f}; // rz -0.5: horizontal right-button drag, ~150 px in 0.5 s
        run(twist, 100, st);
        run(zero, 60, st);
        print("rotate", st);

        st = {};
        const float tilt_up[6] = {0, 0, 0, 0.5f, 0, 0}; // rx +0.5: vertical right-button drag, ~90 px in 0.3 s
        run(tilt_up, 60, st);
        run(zero, 60, st);
        print("rotate_v", st);

        st = {};
        const float push_right[6] = {0.4f, 0, 0, 0, 0, 0}; // x: pan right with middle button, ~100 px
        run(push_right, 100, st);
        run(zero, 60, st);
        print("pan", st);

        st = {};
        const float push_forward[6] = {0, 1.0f, 0, 0, 0, 0}; // y: zoom in, ~4 notches in 0.5 s
        run(push_forward, 100, st);
        run(zero, 20, st);
        print("zoom", st);

        st = {};
        tap_button(0);
        run(zero, 6, st);
        tap_button(1);
        run(zero, 6, st);
        print("taps", st);

        st = {};
        run(twist, 30, st);
        set_enabled(false); // mid gesture
        run(zero, 60, st);
        print("disable_mid_gesture", st);

        g_config = saved;
        set_enabled(false);
    }
#endif

    void print_json(Print& out)
    {
        out.print("{\"t\":\"nm\",\"on\":");
        out.print(g_enabled ? 1 : 0);
        out.print(",\"tg\":[");
        for (int i = 0; i < 6; ++i) {
            if (i > 0) {
                out.print(',');
            }
            out.print(g_config.target[i]);
        }
        out.print("],\"sg\":[");
        for (int i = 0; i < 6; ++i) {
            if (i > 0) {
                out.print(',');
            }
            out.print(g_config.sign[i]);
        }
        out.print("],\"rs\":");
        out.print(g_config.rotate_speed, 1);
        out.print(",\"ps\":");
        out.print(g_config.pan_speed, 1);
        out.print(",\"zs\":");
        out.print(g_config.zoom_speed, 2);
        out.print(",\"rb\":");
        out.print(g_config.rotate_button);
        out.print(",\"pb\":");
        out.print(g_config.pan_button);
        out.print(",\"rm\":");
        out.print(g_config.rotate_modifier);
        out.print(",\"pm\":");
        out.print(g_config.pan_modifier);
        out.print(",\"bm\":[");
        out.print(g_config.button_modifier[0]);
        out.print(',');
        out.print(g_config.button_modifier[1]);
        out.print("],\"bk\":[");
        out.print(g_config.button_key[0]);
        out.print(',');
        out.print(g_config.button_key[1]);
        out.print("],\"rc\":");
        out.print(g_config.restore_cursor);
        out.println('}');
    }
} // namespace NomadMode
