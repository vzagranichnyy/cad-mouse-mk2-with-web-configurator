#pragma once

#include <Arduino.h>
#include <string.h>

// Single-writer value shared between the cores with a sequence lock.
// The writer (Core 0) calls publish(); a reader calls fetch(), which copies a
// consistent snapshot only when the value changed since its last fetch.
template<typename T>
class SeqlockValue {
public:
    void publish(const T& value)
    {
        uint32_t words[WORDS];
        memcpy(words, &value, sizeof(T));

        const uint32_t sequence = m_sequence;
        m_sequence = sequence + 1u;
        __dmb();
        for (size_t i = 0; i < WORDS; ++i) {
            m_words[i] = words[i];
        }
        __dmb();
        m_sequence = sequence + 2u;
    }

    bool fetch(uint32_t& last_sequence, T& out) const
    {
        uint32_t words[WORDS];
        uint32_t first_sequence = 0;
        uint32_t second_sequence = 0;

        do {
            first_sequence = m_sequence;
            if (first_sequence & 1u) {
                continue;
            }
            if (first_sequence == 0 || first_sequence == last_sequence) {
                return false;
            }

            __dmb();
            for (size_t i = 0; i < WORDS; ++i) {
                words[i] = m_words[i];
            }
            __dmb();

            second_sequence = m_sequence;
        } while (first_sequence != second_sequence || (second_sequence & 1u));

        memcpy(&out, words, sizeof(T));
        last_sequence = second_sequence;
        return true;
    }

private:
    static_assert(sizeof(T) % sizeof(uint32_t) == 0, "SeqlockValue needs a word sized type");
    static constexpr size_t WORDS = sizeof(T) / sizeof(uint32_t);

    volatile uint32_t m_sequence = 0;
    volatile uint32_t m_words[WORDS] = {};
};
