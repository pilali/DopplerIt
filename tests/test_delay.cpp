// Offline tests for the DopplerIt delay engine. Build and run with "make test".

#include "delay_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using dopplerit::DelayEngine;
using dopplerit::DelayParams;

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

static DelayParams defaults()
{
    DelayParams p;
    p.shape = dopplerit::kShapeApproach;
    p.stereo = true;
    p.time = 0.5f;
    p.depth = 0.4f;
    p.period = 2.f;
    p.feedback = 0.f;
    p.tone = 1.f;
    p.mix = 1.f;
    p.loop = true;
    return p;
}

struct Render { std::vector<float> l, r; DelayEngine* e; };

// input: sine (amp, freq) or burst; hook may change params / trigger per block
template <class In, class Hook>
static Render render(DelayParams p, double seconds, In input, Hook hook)
{
    Render out;
    out.e = new DelayEngine();
    out.e->init(kFs, 2);
    out.e->reset(p);
    const size_t total = (size_t)(seconds * kFs);
    out.l.resize(total);
    out.r.resize(total);
    std::vector<float> in(128);
    for (size_t pos = 0; pos < total; pos += 128) {
        const uint32_t n = (uint32_t)std::min<size_t>(128, total - pos);
        bool en = true;
        hook(pos / kFs, p, *out.e, en);
        for (uint32_t i = 0; i < n; ++i) in[i] = input((pos + i) / kFs);
        float* outs[2] = { &out.l[pos], &out.r[pos] };
        out.e->process(in.data(), outs, n, p, en);
    }
    return out;
}

struct Sine {
    double f, a;
    float operator()(double t) const { return (float)(a * std::sin(2.0 * kPi * f * t)); }
};
static Sine sine(double f, double a = 0.5) { return Sine { f, a }; }
static void noHook(double, DelayParams&, DelayEngine&, bool&) {}

static double measureFreq(const std::vector<float>& s, double t0, double t1)
{
    const size_t a = (size_t)(t0 * kFs), b = (size_t)(t1 * kFs);
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
    std::printf("[pitch of the repeats follows 1 - dD/dt]\n");
    {
        DelayParams p = defaults();
        // hi = 0.5 s, lo = 0.5 * (1 - 0.95 * 0.4) = 0.31 s, over 2 s: |dD/dt| = 0.095
        const double rate = 0.5 * 0.95 * 0.4 / 2.0;
        Render a = render(p, 6.0, sine(1000.0), noHook);
        const double up = measureFreq(a.l, 2.5, 3.5);
        CHECK(std::fabs(up - 1000.0 * (1.0 + rate)) < 2.0, "Approach: %.1f Hz (theory %.1f)", up, 1000.0 * (1.0 + rate));
        p.shape = dopplerit::kShapeRecede;
        Render b = render(p, 6.0, sine(1000.0), noHook);
        const double down = measureFreq(b.l, 2.5, 3.5);
        CHECK(std::fabs(down - 1000.0 * (1.0 - rate)) < 2.0, "Recede: %.1f Hz (theory %.1f)", down, 1000.0 * (1.0 - rate));
        p.shape = dopplerit::kShapePassBy;
        Render c = render(p, 6.0, sine(1000.0), noHook);
        const double f1 = measureFreq(c.l, 2.2, 2.8), f2 = measureFreq(c.l, 3.2, 3.8);
        CHECK(f1 > 1100.0 && f2 < 900.0, "Pass-by: up (%.1f Hz) then down (%.1f Hz)", f1, f2);
        delete a.e; delete b.e; delete c.e;
    }

    std::printf("[repeats are transposed again at each pass]\n");
    {
        // short burst, feedback: each repeat read during the ramp gets the ratio once more
        DelayParams p = defaults();
        p.time = 0.25f; p.depth = 0.6f; p.period = 16.f; p.feedback = 0.9f;
        auto burst = [](double t) { return t < 0.08 ? (float)(0.5 * std::sin(2.0 * kPi * 1000.0 * t)) : 0.f; };
        Render r = render(p, 1.2, burst, noHook);
        // repeat k is around k * D (D slowly decreasing); measure the first three
        double prev = 1000.0;
        bool rising = true;
        double d = 0.25;
        for (int k = 1; k <= 3; ++k) {
            const double start = k * d * 0.995 + 0.01;
            const double f = measureFreq(r.l, start, start + 0.05);
            std::printf("         repeat %d: %.1f Hz\n", k, f);
            rising = rising && f > prev;
            prev = f;
        }
        CHECK(rising, "pitch rises with every repeat (cumulative)");
        delete r.e;
    }

    std::printf("[stability: feedback 100 %%, loud input]\n");
    {
        DelayParams p = defaults();
        p.feedback = 1.f; p.time = 0.12f; p.period = 0.5f; p.depth = 0.8f;
        Render r = render(p, 20.0, sine(300.0, 0.9), noHook);
        float peak = 0.f;
        bool fin = true;
        for (float v : r.l) { fin = fin && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
        CHECK(fin && peak < 2.5f, "bounded output (peak %.2f)", peak);
        delete r.e;
    }

    std::printf("[one-shot: static until triggered]\n");
    {
        DelayParams p = defaults();
        p.loop = false;
        auto hook = [](double t, DelayParams&, DelayEngine& e, bool&) { if (t >= 3.0 && t < 3.002) e.trigger(); };
        Render r = render(p, 6.0, sine(1000.0), hook);
        const double still = measureFreq(r.l, 1.0, 2.5);
        const double moving = measureFreq(r.l, 3.4, 4.4);
        CHECK(std::fabs(still - 1000.0) < 0.5, "before trigger: no transposition (%.2f Hz)", still);
        CHECK(moving > 1080.0, "after trigger: swept (%.1f Hz)", moving);
        delete r.e;
    }

    std::printf("[no click at the saw fly-back, trigger, parameter jumps]\n");
    {
        // no feedback: the output must stay a continuous sine pitched at most 4x
        DelayParams p = defaults();
        p.period = 0.5f; p.depth = 1.f; p.feedback = 0.f;
        auto hook = [](double t, DelayParams& q, DelayEngine& e, bool&) {
            if (t > 3.0 && t < 3.003) e.trigger();
            if (t > 4.0) q.shape = dopplerit::kShapeRecede;
            if (t > 5.0) q.time = 1.2f;
            if (t > 6.0) q.stereo = false;
            if (t > 7.0) { q.loop = false; q.shape = dopplerit::kShapePassBy; }
        };
        double maxMove = 0.0, prevD = -1.0;
        auto hook2 = [&](double t, DelayParams& q, DelayEngine& e, bool& en) {
            if (prevD >= 0.0) maxMove = std::max(maxMove, std::fabs(e.currentDelay(0) - prevD));
            prevD = e.currentDelay(0);
            hook(t, q, e, en);
        };
        Render r = render(p, 8.0, sine(220.0), hook2);
        // 220 Hz at 0.5, pitched up to 4x: natural step <= 2 pi 880 / 48000 * 0.5 = 0.058
        const float ml = maxStep(r.l, 4800), mr = maxStep(r.r, 4800);
        CHECK(ml < 0.065f && mr < 0.065f, "max sample step L=%.4f R=%.4f (continuous)", ml, mr);
        const double perSample = maxMove * kFs / 128.0;
        CHECK(perSample <= 3.0 + 1e-6, "read head moves at most %.2f samples per sample (limit 3)", perSample);
        delete r.e;
    }

    std::printf("[mono / stereo]\n");
    {
        DelayParams p = defaults();
        p.stereo = false; p.feedback = 0.5f;
        Render m = render(p, 4.0, sine(440.0), noHook);
        float diff = 0.f;
        for (size_t i = 0; i < m.l.size(); ++i) diff = std::max(diff, std::fabs(m.l[i] - m.r[i]));
        CHECK(diff == 0.f, "mono: L == R");
        p.stereo = true;
        Render s = render(p, 4.0, sine(440.0), noHook);
        diff = 0.f;
        for (size_t i = 0; i < s.l.size(); ++i) diff = std::max(diff, std::fabs(s.l[i] - s.r[i]));
        CHECK(diff > 0.05f, "stereo: L and R offset (max diff %.2f)", diff);
        delete m.e; delete s.e;
    }

    std::printf("[bypass]\n");
    {
        DelayParams p = defaults();
        p.feedback = 0.7f;
        auto hook = [](double, DelayParams&, DelayEngine&, bool& en) { en = false; };
        Render r = render(p, 1.0, sine(440.0), hook);
        float diff = 0.f;
        for (size_t i = 24000; i < r.l.size(); ++i)
            diff = std::max(diff, std::fabs(r.l[i] - (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / kFs))));
        CHECK(diff < 1e-4f, "bypassed output equals input (max diff %g)", diff);
        delete r.e;
    }

    std::printf("[memory and CPU]\n");
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 }) {
            DelayEngine e;
            e.init(fs, 2);
            std::printf("         %.0f Hz: 2 lines of %u samples (%.2f MiB)\n", fs, e.bufferSize(),
                        2.0 * e.bufferSize() * 4.0 / 1048576.0);
        }
        DelayParams p = defaults();
        p.feedback = 0.7f; p.tone = 0.6f;
        const auto t0 = std::chrono::steady_clock::now();
        Render r = render(p, 60.0, sine(440.0), noHook);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("         60 s of stereo audio in %.3f s (%.2f %% of real time)\n", el, el / 60.0 * 100.0);
        delete r.e;
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
