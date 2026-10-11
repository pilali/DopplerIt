// Offline tests for the DopplerIt "age" engine (time-invariant Doppler delay).

#include "age_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using dopplerit::AgeDelayEngine;
using dopplerit::AgeParams;

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        std::printf((cond) ? "  ok:   " : "  FAIL: ");    \
        std::printf(__VA_ARGS__);                         \
        std::printf("\n");                                \
        if (!(cond)) ++failures;                          \
    } while (0)

static const double kFs = 48000.0;
static const double kPi = 3.14159265358979323846;

static AgeParams defaults()
{
    AgeParams p;
    p.mode = dopplerit::kAgePassBy;
    p.time = 0.25f;
    p.speedKmh = 100.f;
    p.depth = 1.f;
    p.distance = 5.f;
    p.period = 2.f;
    p.feedback = 0.8f;
    p.tone = 1.f;
    p.mix = 1.f;
    p.stereo = false;
    return p;
}

struct Render { std::vector<float> l, r; };

template <class In, class Hook>
static Render render(AgeParams p, double seconds, In input, Hook hook, AgeDelayEngine* keep = nullptr)
{
    AgeDelayEngine local;
    AgeDelayEngine& e = keep ? *keep : local;
    e.init(kFs, 2);
    e.reset(p);
    Render out;
    const size_t total = (size_t)(seconds * kFs);
    out.l.resize(total);
    out.r.resize(total);
    std::vector<float> in(128);
    for (size_t pos = 0; pos < total; pos += 128) {
        const uint32_t n = (uint32_t)std::min<size_t>(128, total - pos);
        bool en = true;
        hook(pos / kFs, p, en);
        for (uint32_t i = 0; i < n; ++i) in[i] = input((pos + i) / kFs);
        float* outs[2] = { &out.l[pos], &out.r[pos] };
        e.process(in.data(), outs, n, p, en);
    }
    return out;
}

static void noHook(double, AgeParams&, bool&) {}

struct Bursts {
    std::vector<double> at;
    double f, len;
    float operator()(double t) const {
        for (double a : at)
            if (t >= a && t < a + len) {
                const double x = t - a;
                const double env = std::min(1.0, std::min(x, a + len - t) / 0.005);
                return (float)(0.5 * env * std::sin(2.0 * kPi * f * x));
            }
        return 0.f;
    }
};

struct Sine {
    double f, a;
    float operator()(double t) const { return (float)(a * std::sin(2.0 * kPi * f * t)); }
};

static double measureFreq(const std::vector<float>& s, double t0, double t1)
{
    const size_t a = (size_t)(t0 * kFs), b = std::min(s.size(), (size_t)(t1 * kFs));
    double first = -1, last = -1;
    int count = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (s[i - 1] < 0.f && s[i] >= 0.f) {
            const double t = (double)(i - 1) + s[i - 1] / (s[i - 1] - s[i]);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    return count > 0 ? count * kFs / (last - first) : 0.0;
}

static float maxStep(const std::vector<float>& s, size_t from)
{
    float m = 0.f;
    for (size_t i = from; i < s.size(); ++i) m = std::max(m, std::fabs(s[i] - s[i - 1]));
    return m;
}

int main()
{
    std::printf("[each repeat is pitched by the Doppler curve at its age]\n");
    {
        AgeParams p = defaults();
        AgeDelayEngine e;
        // one 1 kHz burst; repeat k plays around 0.3 + k * 0.25 s
        Bursts b { { 0.3 }, 1000.0, 0.15 };
        Render r = render(p, 3.0, b, noHook, &e);
        std::printf("         %d repeats over the pass\n", e.taps());
        double prev = 1e9;
        bool decreasing = true;
        for (int k = 1; k <= 8; ++k) {
            const double t0 = 0.3 + k * 0.25 + 0.03;
            const double f = measureFreq(r.l, t0, t0 + 0.09);
            const double expect = 1000.0 * e.pitchAtAge(k * 0.25);
            std::printf("         repeat %d (age %.2f s): %.1f Hz, curve %.1f Hz\n", k, k * 0.25, f, expect);
            CHECK(std::fabs(f - expect) < 3.0, "repeat %d follows the curve", k);
            decreasing = decreasing && f < prev + 0.5;
            prev = f;
        }
        CHECK(decreasing, "arrival (higher) to departure (lower) as the sound ages");
    }

    std::printf("[time invariance: the same note gets the same treatment whenever it is played]\n");
    {
        AgeParams p = defaults();
        Bursts b { { 0.3, 3.71 }, 660.0, 0.15 };
        Render r = render(p, 7.0, b, noHook);
        double worst = 0.0;
        for (int k = 1; k <= 8; ++k) {
            const double f1 = measureFreq(r.l, 0.3 + k * 0.25 + 0.03, 0.3 + k * 0.25 + 0.12);
            const double f2 = measureFreq(r.l, 3.71 + k * 0.25 + 0.03, 3.71 + k * 0.25 + 0.12);
            worst = std::max(worst, std::fabs(f1 - f2));
        }
        CHECK(worst < 1.0, "repeat pitches identical for notes played at 0.30 s and 3.71 s (max diff %.2f Hz)", worst);
        // timing: each repeat starts within the transposition window of its nominal time
        auto onset = [&](double from) {
            const size_t a = (size_t)(from * kFs), e = (size_t)((from + 0.2) * kFs);
            float pk = 0.f;
            for (size_t i = a; i < e; ++i) pk = std::max(pk, std::fabs(r.l[i]));
            for (size_t i = a; i < e; ++i) if (std::fabs(r.l[i]) > 0.3f * pk) return (double)i / kFs;
            return from;
        };
        double late = 0.0;
        for (int k = 1; k <= 8; ++k) {
            const double n1 = 0.3 + k * 0.25, n2 = 3.71 + k * 0.25;
            late = std::max(late, std::max(std::fabs(onset(n1 - 0.05) - n1), std::fabs(onset(n2 - 0.05) - n2)));
        }
        CHECK(late < 0.017, "every repeat lands within %.1f ms of its nominal time (window +/- 15 ms)", late * 1000.0);
    }

    std::printf("[modes]\n");
    {
        AgeParams p = defaults();
        Bursts b { { 0.3 }, 1000.0, 0.15 };
        p.mode = dopplerit::kAgeApproach;
        Render a = render(p, 3.0, b, noHook);
        const double a1 = measureFreq(a.l, 0.58, 0.67), a8 = measureFreq(a.l, 2.33, 2.42);
        CHECK(a1 > 1060.0 && std::fabs(a8 - 1000.0) < 15.0, "Approach: %.1f Hz -> %.1f Hz", a1, a8);
        p.mode = dopplerit::kAgeRecede;
        Render rc = render(p, 3.0, b, noHook);
        AgeDelayEngine ref;
        ref.init(kFs, 1);
        ref.reset(p);
        const double r1 = measureFreq(rc.l, 0.58, 0.67), r8 = measureFreq(rc.l, 2.33, 2.42);
        const double e1 = 1000.0 * ref.pitchAtAge(0.25), e8 = 1000.0 * ref.pitchAtAge(2.0);
        CHECK(std::fabs(r1 - e1) < 3.0 && std::fabs(r8 - e8) < 3.0 && r1 > r8,
              "Recede: %.1f Hz -> %.1f Hz (curve %.1f -> %.1f), starting nearer the original pitch", r1, r8, e1, e8);
    }

    std::printf("[tail keeps the final pitch until the sound fades]\n");
    {
        AgeParams p = defaults();
        p.period = 1.f; // 4 repeats in the pass, then the tail
        AgeDelayEngine e;
        Bursts b { { 0.3 }, 1000.0, 0.15 };
        Render r = render(p, 4.0, b, noHook, &e);
        const double f6 = measureFreq(r.l, 0.3 + 6 * 0.25 + 0.03, 0.3 + 6 * 0.25 + 0.12);
        const double fend = 1000.0 * e.pitchAtAge(1.0);
        CHECK(std::fabs(f6 - fend) < 3.0, "repeat 6 (in the tail): %.1f Hz = final pitch %.1f Hz", f6, fend);
    }

    std::printf("[depth: exaggeration up to one octave each way]\n");
    {
        AgeParams p = defaults();
        p.speedKmh = 300.f;
        p.depth = 8.f;
        AgeDelayEngine e;
        Bursts b { { 0.3 }, 1000.0, 0.15 };
        Render r = render(p, 3.0, b, noHook, &e);
        const double up = measureFreq(r.l, 0.58, 0.67), down = measureFreq(r.l, 2.33, 2.42);
        CHECK(std::fabs(up - 1000.0 * e.pitchAtAge(0.25)) < 6.0 && up > 1980.0,
              "highest repeat %.1f Hz (one octave up: 2000)", up);
        CHECK(std::fabs(down - 1000.0 * e.pitchAtAge(2.0)) < 3.0 && down < 505.0,
              "lowest repeat %.1f Hz (one octave down: 500)", down);
        AgeDelayEngine ref;
        ref.init(kFs, 1);
        AgeParams q = defaults();
        ref.reset(q);
        const double real = 12.0 * std::log2(ref.pitchAtAge(0.0));
        q.depth = 3.f;
        ref.reset(q);
        const double x3 = 12.0 * std::log2(ref.pitchAtAge(0.0));
        CHECK(std::fabs(x3 - 3.0 * real) < 0.01, "depth x3 triples the transposition (%.2f -> %.2f semitones)", real, x3);
    }

    std::printf("[long and dense pass]\n");
    {
        AgeParams p = defaults();
        p.time = 0.1f;
        p.period = 2.4f;
        AgeDelayEngine e;
        Bursts b { { 0.3 }, 1000.0, 0.06 };
        Render r = render(p, 3.5, b, noHook, &e);
        const double f1 = measureFreq(r.l, 0.41, 0.45), f24 = measureFreq(r.l, 2.71, 2.75);
        CHECK(e.taps() == 24 && f1 > 1060.0 && f24 < 940.0,
              "%d repeats in a 2.4 s pass: %.1f Hz -> %.1f Hz", e.taps(), f1, f24);
        p.time = 0.5f;
        p.period = 16.f;
        AgeDelayEngine e2;
        render(p, 0.1, b, noHook, &e2);
        CHECK(e2.taps() == 24 || e2.taps() == 32 / 2 + 8, "16 s pass with 0.5 s repeats: %d repeats", e2.taps());
    }

    std::printf("[continuous signal, parameter changes: no discontinuity]\n");
    {
        AgeParams p = defaults();
        p.feedback = 0.5f;
        p.stereo = true;
        auto hook = [](double t, AgeParams& q, bool&) {
            if (t > 2.0) q.mode = dopplerit::kAgeApproach;
            if (t > 3.0) q.time = 0.31f;
            if (t > 4.0) q.period = 0.8f;
            if (t > 5.0) { q.speedKmh = 250.f; q.depth = 2.f; }
            if (t > 6.0) q.stereo = false;
        };
        Render r = render(p, 7.0, Sine { 220.0, 0.5 }, hook);
        // total amplitude <= 0.5 / (1 - 0.5) = 1, pitch <= 1.6x (depth 2), splices/glides x2 margin
        const float lim = (float)(2.0 * kPi * 220.0 * 1.6 / kFs * 1.0 * 2.0);
        const float ml = maxStep(r.l, 9600), mr = maxStep(r.r, 9600);
        CHECK(ml < lim && mr < lim, "max sample step L=%.4f R=%.4f (bound %.4f)", ml, mr, lim);
        bool fin = true;
        for (float v : r.l) fin = fin && std::isfinite(v);
        CHECK(fin, "finite output");
    }

    std::printf("[stability, mono, bypass]\n");
    {
        AgeParams p = defaults();
        p.feedback = 0.98f; p.time = 0.05f; p.period = 0.6f; p.speedKmh = 300.f;
        Render r = render(p, 20.0, Sine { 300.0, 0.9 }, noHook);
        float peak = 0.f;
        bool fin = true;
        for (float v : r.l) { fin = fin && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
        CHECK(fin && peak < 60.f, "feedback 98 %%: finite (peak %.1f, linear delay sum)", peak);

        p = defaults();
        p.stereo = false;
        Render m = render(p, 3.0, Sine { 440.0, 0.5 }, noHook);
        float d = 0.f;
        for (size_t i = 0; i < m.l.size(); ++i) d = std::max(d, std::fabs(m.l[i] - m.r[i]));
        CHECK(d == 0.f, "mono: L == R");
        auto off = [](double, AgeParams&, bool& en) { en = false; };
        Render b = render(p, 1.0, Sine { 440.0, 0.5 }, off);
        d = 0.f;
        for (size_t i = 24000; i < b.l.size(); ++i)
            d = std::max(d, std::fabs(b.l[i] - (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / kFs))));
        CHECK(d < 1e-4f, "bypass: output equals input");
    }

    std::printf("[memory and CPU]\n");
    {
        AgeDelayEngine e;
        e.init(48000.0, 2);
        std::printf("         48 kHz: line %u samples x 2 channels (%.1f MiB)\n", e.bufferSize(),
                    2.0 * e.bufferSize() * 4.0 / 1048576.0);
        for (int heavy = 0; heavy < 2; ++heavy) {
            AgeParams p = defaults();
            p.time = heavy ? 0.1f : 0.25f; p.period = heavy ? 2.4f : 2.f; p.stereo = true;
            p.feedback = 0.6f; p.tone = 0.6f; p.depth = heavy ? 4.f : 1.f;
            const auto t0 = std::chrono::steady_clock::now();
            AgeDelayEngine e3;
            render(p, 60.0, Sine { 440.0, 0.5 }, noHook, &e3);
            const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("         60 s, %d taps, depth x%.0f, stereo: %.3f s (%.2f %% of real time), %u splices\n",
                        e3.taps(), p.depth, el, el / 60.0 * 100.0, e3.splices());
        }
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
