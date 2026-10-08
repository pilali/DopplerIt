// Offline tests for the DopplerIt multi-head Doppler delay. Run with "make test".

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
static const double kC  = 343.0;

static DelayParams defaults()
{
    DelayParams p;
    p.heads = 1;
    p.time = 0.5f;
    p.speedKmh = 100.f;
    p.distance = 2.f;
    p.period = 4.f;
    p.stagger = 0.f;
    p.feedback = 0.f;
    p.tone = 1.f;
    p.mix = 1.f;
    p.stereo = true;
    p.loop = true;
    return p;
}

struct Sine {
    double f, a;
    float operator()(double t) const { return (float)(a * std::sin(2.0 * kPi * f * t)); }
};
static Sine sine(double f, double a = 0.5) { return Sine { f, a }; }

struct Render { std::vector<float> l, r; };

template <class In, class Hook>
static Render render(DelayParams p, double seconds, In input, Hook hook)
{
    DelayEngine e;
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
        hook(pos / kFs, p, e, en);
        for (uint32_t i = 0; i < n; ++i) in[i] = input((pos + i) / kFs);
        float* outs[2] = { &out.l[pos], &out.r[pos] };
        e.process(in.data(), outs, n, p, en);
    }
    return out;
}

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

// energy of a frequency over a window (Goertzel, Hann window)
static double energyAt(const std::vector<float>& s, double t0, double t1, double f)
{
    const size_t a = (size_t)(t0 * kFs), b = (size_t)(t1 * kFs);
    const double w = 2.0 * kPi * f / kFs;
    double re = 0.0, im = 0.0;
    for (size_t i = a; i < b; ++i) {
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)(i - a) / (double)(b - a));
        re += s[i] * win * std::cos(w * (double)i);
        im += s[i] * win * std::sin(w * (double)i);
    }
    return re * re + im * im;
}

static float maxStep(const std::vector<float>& s, size_t from)
{
    float m = 0.f;
    for (size_t i = from; i < s.size(); ++i) m = std::max(m, std::fabs(s[i] - s[i - 1]));
    return m;
}

int main()
{
    const double k = 100.0 / 3.6 / kC; // v/c at 100 km/h

    std::printf("[pass-by: arrival and departure plateaus at 1 +/- v/c]\n");
    {
        DelayParams p = defaults();
        Render r = render(p, 9.0, sine(1000.0), noHook);
        // second cycle: 4..8 s; arrival around 4.4-5.4 s, departure 6.6-7.6 s
        const double up = measureFreq(r.l, 4.4, 5.4), down = measureFreq(r.l, 6.6, 7.6);
        CHECK(std::fabs(up - 1000.0 * (1.0 + k)) < 3.0, "arrival %.1f Hz (theory %.1f)", up, 1000.0 * (1.0 + k));
        CHECK(std::fabs(down - 1000.0 * (1.0 - k)) < 3.0, "departure %.1f Hz (theory %.1f)", down, 1000.0 * (1.0 - k));
        // the pitch flips instantly from departure to arrival at the end of a cycle
        const double before = measureFreq(r.l, 7.88, 7.99), after = measureFreq(r.l, 8.01, 8.12);
        CHECK(before < 940.0 && after > 1060.0, "brutal return: %.1f Hz -> %.1f Hz across the cycle end", before, after);
        CHECK(maxStep(r.l, 4800) < 0.5 * 2.0 * kPi * 1000.0 * (1.0 + k) / kFs * 1.02,
              "continuous output across the jump (max step %.4f)", maxStep(r.l, 4800));
    }

    std::printf("[sharpness: distance controls the pitch drop at the closest point]\n");
    {
        DelayParams p = defaults();
        p.distance = 1.f;
        Render sharp = render(p, 9.0, sine(1000.0), noHook);
        p.distance = 50.f;
        Render soft = render(p, 9.0, sine(1000.0), noHook);
        // 0.1 s after the closest point (6.0 s): sharp is already down, soft still gliding
        const double fs = measureFreq(sharp.l, 6.08, 6.18), fo = measureFreq(soft.l, 6.08, 6.18);
        CHECK(fs < 940.0 && fo > 960.0, "1 m: %.1f Hz, 50 m: %.1f Hz just after the closest point", fs, fo);
    }

    std::printf("[heads in series compound the transposition]\n");
    {
        DelayParams p = defaults();
        p.heads = 2;
        p.time = 0.3f;
        Render r = render(p, 9.0, sine(1000.0), noHook);
        const double f1 = 1000.0 * (1.0 + k), f2 = 1000.0 * (1.0 + k) * (1.0 + k);
        const double e1 = energyAt(r.l, 4.6, 5.4, f1), e2 = energyAt(r.l, 4.6, 5.4, f2), e0 = energyAt(r.l, 4.6, 5.4, 1000.0);
        CHECK(e1 > 20.0 * e0 && e2 > 20.0 * e0, "arrival: head 1 at %.0f Hz and head 2 at %.0f Hz (x%.1f / x%.1f vs 1 kHz)",
              f1, f2, e1 / e0, e2 / e0);
    }

    std::printf("[stability: 4 heads, feedback 100 %%, loud input]\n");
    {
        DelayParams p = defaults();
        p.heads = 4; p.feedback = 1.f; p.time = 0.08f; p.period = 0.7f; p.speedKmh = 300.f;
        Render r = render(p, 20.0, sine(300.0, 0.9), noHook);
        float peak = 0.f;
        bool fin = true;
        for (float v : r.l) { fin = fin && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
        CHECK(fin && peak < 3.f, "bounded output (peak %.2f)", peak);
    }

    std::printf("[one-shot: static until triggered]\n");
    {
        DelayParams p = defaults();
        p.loop = false;
        auto hook = [](double t, DelayParams&, DelayEngine& e, bool&) { if (t >= 3.0 && t < 3.002) e.trigger(); };
        Render r = render(p, 6.0, sine(1000.0), hook);
        const double still = measureFreq(r.l, 1.0, 2.5), moving = measureFreq(r.l, 3.3, 4.2);
        CHECK(std::fabs(still - 1000.0) < 0.5, "before trigger: no transposition (%.2f Hz)", still);
        CHECK(moving > 1060.0, "after trigger: arrival (%.1f Hz)", moving);
    }

    std::printf("[no discontinuity on parameter changes]\n");
    {
        DelayParams p = defaults();
        p.heads = 4; p.time = 0.2f; p.period = 1.f; p.speedKmh = 150.f;
        double maxMove = 0.0, prevD = -1.0;
        auto hook = [&](double t, DelayParams& q, DelayEngine& e, bool&) {
            if (prevD >= 0.0) maxMove = std::max(maxMove, std::fabs(e.currentDelay(0, 0) - prevD));
            prevD = e.currentDelay(0, 0);
            if (t > 2.0 && t < 2.003) e.trigger();
            if (t > 3.0) q.heads = 2;
            if (t > 4.0) q.stagger = 0.5f;
            if (t > 5.0) q.time = 0.6f;
            if (t > 6.0) q.stereo = false;
            if (t > 7.0) { q.heads = 3; q.loop = false; }
        };
        Render r = render(p, 8.0, sine(220.0), hook);
        // 4 coherent heads (total amplitude <= 1) at up to 4x pitch (pass-by and time knob jumps)
        const float lim = (float)(2.0 * kPi * 220.0 * 4.0 / kFs * 1.0);
        const float ml = maxStep(r.l, 4800), mr = maxStep(r.r, 4800);
        CHECK(ml < lim && mr < lim, "max sample step L=%.4f R=%.4f (bound %.4f)", ml, mr, lim);
        CHECK(maxMove * kFs / 128.0 <= 1.0 + 1e-6, "read head speed limited (%.2f samples per sample)", maxMove * kFs / 128.0);
    }

    std::printf("[mono / stereo / bypass]\n");
    {
        DelayParams p = defaults();
        p.heads = 3; p.feedback = 0.5f; p.stereo = false;
        Render m = render(p, 4.0, sine(440.0), noHook);
        float diff = 0.f;
        for (size_t i = 0; i < m.l.size(); ++i) diff = std::max(diff, std::fabs(m.l[i] - m.r[i]));
        CHECK(diff == 0.f, "mono: L == R");
        p.stereo = true;
        Render s = render(p, 4.0, sine(440.0), noHook);
        diff = 0.f;
        for (size_t i = 0; i < s.l.size(); ++i) diff = std::max(diff, std::fabs(s.l[i] - s.r[i]));
        CHECK(diff > 0.02f, "stereo: L and R slightly offset (max diff %.2f)", diff);
        auto off = [](double, DelayParams&, DelayEngine&, bool& en) { en = false; };
        Render b = render(p, 1.0, sine(440.0), off);
        diff = 0.f;
        for (size_t i = 24000; i < b.l.size(); ++i)
            diff = std::max(diff, std::fabs(b.l[i] - (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / kFs))));
        CHECK(diff < 1e-4f, "bypass: output equals input (max diff %g)", diff);
    }

    std::printf("[memory and CPU]\n");
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 }) {
            DelayEngine e;
            e.init(fs, 2);
            std::printf("         %.0f Hz: 8 lines of %u samples (%.1f MiB)\n", fs, e.bufferSize(),
                        8.0 * e.bufferSize() * 4.0 / 1048576.0);
        }
        DelayParams p = defaults();
        p.heads = 4; p.feedback = 0.7f; p.tone = 0.6f; p.stagger = 0.25f;
        const auto t0 = std::chrono::steady_clock::now();
        Render r = render(p, 60.0, sine(440.0), noHook);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("         60 s, 4 heads, stereo: %.3f s (%.2f %% of real time)\n", el, el / 60.0 * 100.0);
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
