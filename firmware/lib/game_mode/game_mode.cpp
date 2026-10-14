#include "game_mode.h"

#include <math.h>
#include <string.h>

#include "storage.h"

namespace GameMode {
    namespace {
        const char* const GAME_MODE_PATH = "/game_mode.bin";
        const uint32_t GAME_MODE_MAGIC = 0x474D4D43u; // "CMMG"
        const uint16_t GAME_MODE_VERSION = 1;
        const float RELEASE_RATIO = 0.7f; // A key releases at 70% of its threshold, so it does not chatter

        // HID keyboard usage IDs
        const uint8_t KEY_A = 0x04;
        const uint8_t KEY_C = 0x06;
        const uint8_t KEY_D = 0x07;
        const uint8_t KEY_E = 0x08;
        const uint8_t KEY_F = 0x09;
        const uint8_t KEY_Q = 0x14;
        const uint8_t KEY_R = 0x15;
        const uint8_t KEY_S = 0x16;
        const uint8_t KEY_W = 0x1A;
        const uint8_t KEY_SPACE = 0x2C;
        const uint8_t FIRST_MODIFIER = 0xE0;
        const uint8_t LAST_MODIFIER = 0xE7;

        Config g_config = defaults();
        bool g_enabled = false;
        uint16_t g_active = 0;

        void sanitize(Config& config)
        {
            config.translation_threshold = fminf(fmaxf(config.translation_threshold, 0.1f), 0.9f);
            config.rotation_threshold = fminf(fmaxf(config.rotation_threshold, 0.1f), 0.9f);
            config.reserved[0] = 0;
            config.reserved[1] = 0;
        }

        bool update_axis(uint16_t& active, int binding, float value, float threshold)
        {
            const uint16_t bit = 1u << binding;
            const float limit = (active & bit) ? threshold * RELEASE_RATIO : threshold;
            if (value > limit) {
                active |= bit;
                return true;
            }
            active &= ~bit;
            return false;
        }
    } // namespace

    Config defaults()
    {
        // Matches the default GameLoop key mapping for PUBG Mobile
        Config config = {};
        config.translation_threshold = 0.35f;
        config.rotation_threshold = 0.5f;
        config.keys[X_POS] = KEY_D;
        config.keys[X_NEG] = KEY_A;
        config.keys[Y_POS] = KEY_W;
        config.keys[Y_NEG] = KEY_S;
        config.keys[Z_POS] = KEY_SPACE; // jump
        config.keys[Z_NEG] = KEY_C;     // crouch
        config.keys[RY_POS] = KEY_E;    // lean right
        config.keys[RY_NEG] = KEY_Q;    // lean left
        config.keys[BUTTON_LEFT] = KEY_R;  // reload
        config.keys[BUTTON_RIGHT] = KEY_F; // interact
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
        g_active = 0;
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
        return Storage::save(GAME_MODE_PATH, GAME_MODE_MAGIC, GAME_MODE_VERSION, &g_config, sizeof(Config));
    }

    bool load(Config& out)
    {
        Config config;
        if (!Storage::load(GAME_MODE_PATH, GAME_MODE_MAGIC, GAME_MODE_VERSION, &config, sizeof(Config))) {
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
        g_enabled = enabled;
        g_active = 0;
    }

    void make_report(const float pose[6], uint16_t buttons, uint8_t& modifier, uint8_t keys[6])
    {
        modifier = 0;
        memset(keys, 0, 6);
        if (!g_enabled) {
            g_active = 0;
            return;
        }

        for (int axis = 0; axis < 6; ++axis) {
            const float threshold = axis < 3 ? g_config.translation_threshold : g_config.rotation_threshold;
            update_axis(g_active, axis * 2, pose[axis], threshold);
            update_axis(g_active, axis * 2 + 1, -pose[axis], threshold);
        }
        g_active = (g_active & ~((1u << BUTTON_LEFT) | (1u << BUTTON_RIGHT))) |
                   ((buttons & 0x0001) ? (1u << BUTTON_LEFT) : 0) |
                   ((buttons & 0x0002) ? (1u << BUTTON_RIGHT) : 0);

        int count = 0;
        for (int binding = 0; binding < BINDING_COUNT; ++binding) {
            const uint8_t key = g_config.keys[binding];
            if (!(g_active & (1u << binding)) || key == 0) {
                continue;
            }
            if (key >= FIRST_MODIFIER && key <= LAST_MODIFIER) {
                modifier |= 1u << (key - FIRST_MODIFIER);
            }
            else if (count < 6 && memchr(keys, key, count) == nullptr) {
                keys[count++] = key; // A boot keyboard report holds up to 6 keys
            }
        }
    }

    uint16_t active_mask()
    {
        return g_active;
    }

    void print_json(Print& out)
    {
        out.print("{\"t\":\"gm\",\"on\":");
        out.print(g_enabled ? 1 : 0);
        out.print(",\"tt\":");
        out.print(g_config.translation_threshold, 3);
        out.print(",\"rt\":");
        out.print(g_config.rotation_threshold, 3);
        out.print(",\"k\":[");
        for (int i = 0; i < BINDING_COUNT; ++i) {
            if (i > 0) {
                out.print(',');
            }
            out.print(g_config.keys[i]);
        }
        out.println("]}");
    }
} // namespace GameMode
