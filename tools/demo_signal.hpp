// Shared synthetic test signal and WAV writer for the demo renderers.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static const double kFs = 48000.0;
static const double kDur = 16.0;

// Karplus-Strong plucked string
static inline void pluck(std::vector<float>& out, double start, double freq, double dur, float amp, uint32_t& seed)
{
    const size_t n = (size_t)(kFs / freq);
    std::vector<float> line(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((float)(seed >> 9) / 4194304.f - 1.f) * amp;
    }
    for (size_t i = 1; i < n; ++i)
        line[i] = 0.5f * (line[i] + line[i - 1]);
    const size_t s0 = (size_t)(start * kFs), len = (size_t)(dur * kFs);
    size_t idx = 0;
    for (size_t i = 0; i < len && s0 + i < out.size(); ++i) {
        const size_t next = (idx + 1) % n;
        const float y = line[idx];
        line[idx] = 0.994f * 0.5f * (line[idx] + line[next]);
        idx = next;
        // short release (muted note)
        const float rel = i > len - 2400 ? (float)(len - i) / 2400.f : 1.f;
        out[s0 + i] += y * rel;
    }
}

static inline std::vector<float> testSignal()
{
    std::vector<float> s((size_t)(kDur * kFs), 0.f);
    uint32_t seed = 4242u;
    // detached notes
    pluck(s, 0.2, 196.00, 0.35, 0.6f, seed);   // G3
    pluck(s, 2.2, 293.66, 0.35, 0.6f, seed);   // D4
    pluck(s, 4.2, 246.94, 0.35, 0.6f, seed);   // B3
    // short phrase
    const double phrase[] = { 329.63, 392.00, 440.00, 392.00, 329.63, 293.66 };
    for (int i = 0; i < 6; ++i)
        pluck(s, 6.5 + i * 0.25, phrase[i], 0.22, 0.5f, seed);
    // chord
    for (double f : { 98.00, 146.83, 196.00, 246.94 })
        pluck(s, 9.0, f, 0.6, 0.35f, seed);
    // sustained note (soft organ) to hear the whole pass-by curve
    for (size_t i = (size_t)(11.0 * kFs); i < (size_t)(14.0 * kFs); ++i) {
        const double t = (double)i / kFs - 11.0;
        const double env = std::min(1.0, t / 0.05) * std::min(1.0, (3.0 - t) / 0.2);
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin(2.0 * 3.14159265358979 * 330.0 * h * t) / (h * h);
        s[i] += (float)(0.35 * env * v);
    }

    float peak = 0.f;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    for (float& v : s) v *= 0.4f / peak;
    return s;
}

static inline void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
static inline void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

static inline void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    const uint32_t frames = (uint32_t)l.size();
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4);
    std::fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, (uint32_t)kFs); put32(f, (uint32_t)kFs * 4); put16(f, 4); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, frames * 4);
    for (uint32_t i = 0; i < frames; ++i)
        for (float v : { l[i], r[i] }) {
            const float c = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
            put16(f, (uint16_t)(int16_t)std::lrint(c * 32767.f));
        }
    std::fclose(f);
}

