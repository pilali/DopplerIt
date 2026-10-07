// Render comparison WAV files of the DopplerIt engines from a synthetic test
// signal (plucked-string arpeggio, then a sustained chord).
//
//   g++ -O2 -std=c++11 -Isrc tools/render_demo.cpp -o render_demo
//   ./render_demo <output directory>

#include "doppler_engine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using dopplerit::Engine;
using dopplerit::Params;

static const double kFs = 48000.0;
static const double kPi = 3.14159265358979323846;

// --- test signal --------------------------------------------------------------

// Karplus-Strong plucked string
static void pluck(std::vector<float>& out, size_t start, double freq, double dur, float amp, uint32_t& seed)
{
    const size_t n = (size_t)(kFs / freq);
    std::vector<float> line(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((float)(seed >> 9) / 4194304.f - 1.f) * amp;
    }
    // soften the attack (pick position)
    for (size_t i = 1; i < n; ++i)
        line[i] = 0.5f * (line[i] + line[i - 1]);
    const size_t len = (size_t)(dur * kFs);
    size_t idx = 0;
    for (size_t i = 0; i < len && start + i < out.size(); ++i) {
        const size_t next = (idx + 1) % n;
        const float y = line[idx];
        line[idx] = 0.996f * 0.5f * (line[idx] + line[next]);
        idx = next;
        out[start + i] += y;
    }
}

static std::vector<float> testSignal()
{
    std::vector<float> s((size_t)(12.0 * kFs), 0.f);
    uint32_t seed = 12345u;

    // 0 - 6 s: E minor arpeggio, eighth notes at 120 BPM
    const double notes[] = { 82.41, 123.47, 164.81, 196.00, 246.94, 329.63, 246.94, 196.00 };
    for (int i = 0; i < 24; ++i)
        pluck(s, (size_t)(i * 0.25 * kFs), notes[i % 8], 1.5, 0.5f, seed);

    // 6 - 12 s: sustained chord (band-limited saws through a gentle low-pass)
    const double chord[] = { 164.81, 196.00, 246.94, 329.63 };
    double lp = 0.0;
    for (size_t i = (size_t)(6.0 * kFs); i < s.size(); ++i) {
        const double t = (double)i / kFs - 6.0;
        double v = 0.0;
        for (double f : chord)
            for (int h = 1; h * f < 6000.0; ++h)
                v += std::sin(2.0 * kPi * h * f * t + h) / h;
        const double env = std::min(1.0, t / 0.05) * std::min(1.0, (12.0 - 6.0 - t) / 0.3);
        lp += 0.25 * (v - lp);
        s[i] += (float)(0.12 * lp * env);
    }

    float peak = 0.f;
    for (float v : s) peak = std::max(peak, std::fabs(v));
    for (float& v : s) v *= 0.5f / peak;
    return s;
}

// --- WAV ----------------------------------------------------------------------

static void put32(FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
static void put16(FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

static void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    const uint32_t frames = (uint32_t)l.size();
    std::fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4);
    std::fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, (uint32_t)kFs); put32(f, (uint32_t)kFs * 4); put16(f, 4); put16(f, 16);
    std::fwrite("data", 1, 4, f); put32(f, frames * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        for (float v : { l[i], r[i] }) {
            const float c = v > 1.f ? 1.f : (v < -1.f ? -1.f : v);
            put16(f, (uint16_t)(int16_t)std::lrint(c * 32767.f));
        }
    }
    std::fclose(f);
}

// --- renders ------------------------------------------------------------------

static void render(const std::string& dir, const char* name, const std::vector<float>& in, Params p)
{
    Engine e;
    e.init(kFs, 2);
    e.reset(p);
    std::vector<float> l(in.size()), r(in.size());
    const uint32_t block = 128;
    for (size_t pos = 0; pos < in.size(); pos += block) {
        const uint32_t n = (uint32_t)std::min<size_t>(block, in.size() - pos);
        float* outs[2] = { &l[pos], &r[pos] };
        e.process(&in[pos], outs, n, p, true);
    }
    writeWav(dir + "/" + name, l, r);
    std::printf("%-34s max latency %7.1f ms, %4u splices\n", name, e.maxReadDelay() * 1000.0, e.spliceCount());
}

// Which instant of the input is heard at each output instant: feed a ramp
// (input = time in seconds) with every level modulation disabled.
static void timeline(const std::string& dir, int mode, float kmh, float period)
{
    FILE* f = std::fopen((dir + "/timeline.csv").c_str(), mode == dopplerit::kModeApproach ? "w" : "a");
    if (!f) return;
    for (int eng = 0; eng < 2; ++eng) {
        Params p;
        p.mode = mode; p.engine = eng; p.speedKmh = kmh; p.period = period; p.distance = 4.f;
        p.attenuation = 0.f; p.width = 0.f; p.stereo = false; p.mix = 1.f; p.loop = true;
        Engine e;
        e.init(kFs, 1);
        e.reset(p);
        const size_t total = (size_t)(12.0 * kFs);
        std::vector<float> in(total), out(total);
        for (size_t i = 0; i < total; ++i) in[i] = (float)((double)i / kFs);
        for (size_t pos = 0; pos < total; pos += 128) {
            float* outs[1] = { &out[pos] };
            e.process(&in[pos], outs, (uint32_t)std::min<size_t>(128, total - pos), p, true);
        }
        for (size_t i = 0; i < total; i += 48)
            std::fprintf(f, "%d,%d,%.5f,%.5f\n", mode, eng, (double)i / kFs, out[i]);
    }
    std::fclose(f);
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::vector<float> in = testSignal();
    writeWav(dir + "/00_dry.wav", in, in);

    Params base;
    base.mode = dopplerit::kModePassBy;
    base.engine = dopplerit::kEngineStream;
    base.speedKmh = 120.f;
    base.period = 4.f;
    base.distance = 4.f;
    base.attenuation = 0.5f;
    base.width = 0.5f;
    base.stereo = true;
    base.mix = 1.f; // wet only, to hear the processing clearly
    base.loop = true;

    Params p = base;
    p.engine = dopplerit::kEngineTape;
    render(dir, "01_passby_tape.wav", in, p);
    p.engine = dopplerit::kEngineStream;
    render(dir, "02_passby_stream.wav", in, p);

    p = base;
    p.mode = dopplerit::kModeApproach;
    p.speedKmh = 150.f;
    p.period = 3.f;
    p.engine = dopplerit::kEngineTape;
    render(dir, "03_approach_tape.wav", in, p);
    p.engine = dopplerit::kEngineStream;
    render(dir, "04_approach_stream.wav", in, p);

    p = base;
    p.mode = dopplerit::kModeOrbit;
    p.speedKmh = 25.f;      // horn speed ~7 m/s
    p.period = 0.15f;       // ~6.7 rev/s, radius 17 cm
    p.distance = 1.f;
    p.attenuation = 0.3f;
    p.width = 0.7f;
    render(dir, "05_orbit_leslie_fast.wav", in, p);
    p.period = 1.2f;        // slow "chorale" speed
    p.speedKmh = 5.f;
    render(dir, "06_orbit_leslie_slow.wav", in, p);

    p = base;
    p.mode = dopplerit::kModeOrbit;
    p.speedKmh = 50.f;
    p.period = 4.f;         // radius 8.8 m around a centre 15 m away
    p.distance = 15.f;
    p.attenuation = 0.5f;
    p.width = 0.8f;
    render(dir, "07_orbit_wide.wav", in, p);

    p = base;
    p.mode = dopplerit::kModeSwing;
    p.speedKmh = 40.f;
    p.period = 2.f;
    p.distance = 3.f;
    p.width = 0.6f;
    render(dir, "08_swing.wav", in, p);

    p = base;
    p.mix = 0.5f;
    render(dir, "09_passby_stream_mix50.wav", in, p);

    timeline(dir, dopplerit::kModeApproach, 150.f, 3.f);
    timeline(dir, dopplerit::kModePassBy, 120.f, 4.f);
    return 0;
}
