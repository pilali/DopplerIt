// Offline tests for the DopplerIt engine: physics, clicks, mono compatibility,
// bypass and CPU cost. Build and run with "make test".

#include "doppler_engine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using dopplerit::Engine;
using dopplerit::Params;

static int failures = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            std::printf("  FAIL: " __VA_ARGS__);          \
            std::printf("\n");                            \
            ++failures;                                   \
        } else {                                          \
            std::printf("  ok:   " __VA_ARGS__);          \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

static const double kFs = 48000.0;
static const uint32_t kBlock = 128;

static int gEngine = dopplerit::kEngineTape;

static Params defaults()
{
    Params p;
    p.mode = dopplerit::kModePassBy;
    p.engine = gEngine;
    p.speedKmh = 100.f;
    p.period = 4.f;
    p.distance = 4.f;
    p.attenuation = 0.f;
    p.width = 0.f;
    p.stereo = true;
    p.mix = 1.f;
    p.loop = true;
    return p;
}

struct Render {
    std::vector<float> l, r;
};

// Render a sine through the engine. "hook" may change params per block.
template <class Hook>
static Render render(int channels, Params p, double seconds, double freq, Hook hook, Engine** keep = nullptr)
{
    Engine* e = new Engine();
    e->init(kFs, channels);
    e->reset(p);

    const uint32_t total = (uint32_t)(seconds * kFs);
    Render out;
    out.l.resize(total);
    out.r.resize(total);
    std::vector<float> in(kBlock);
    double ph = 0.0;

    for (uint32_t pos = 0; pos < total; pos += kBlock) {
        const uint32_t n = std::min<uint32_t>(kBlock, total - pos);
        bool enabled = true;
        hook(pos, p, *e, enabled);
        for (uint32_t i = 0; i < n; ++i) {
            in[i] = (float)(0.5 * std::sin(ph));
            ph += 2.0 * M_PI * freq / kFs;
        }
        float* outs[2] = { &out.l[pos], &out.r[pos] };
        e->process(in.data(), outs, n, p, enabled);
    }
    if (keep) *keep = e; else delete e;
    return out;
}

static void noHook(uint32_t, Params&, Engine&, bool&) {}

// Average frequency over [t0, t1] via zero crossings
static double measureFreq(const std::vector<float>& s, double t0, double t1)
{
    const size_t a = (size_t)(t0 * kFs), b = (size_t)(t1 * kFs);
    double first = -1, last = -1;
    int count = 0;
    for (size_t i = a + 1; i < b; ++i) {
        if (s[i - 1] < 0.f && s[i] >= 0.f) {
            const double t = (double)(i - 1) + s[i - 1] / (s[i - 1] - s[i]);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    }
    return count > 0 ? count * kFs / (last - first) : 0.0;
}

static float maxStep(const std::vector<float>& s, size_t from = 1)
{
    float m = 0.f;
    for (size_t i = from; i < s.size(); ++i)
        m = std::max(m, std::fabs(s[i] - s[i - 1]));
    return m;
}

static bool finite(const std::vector<float>& s)
{
    for (float v : s) if (!std::isfinite(v)) return false;
    return true;
}

// Local frequency (Hz) over successive windows, via zero crossings
static void freqRange(const std::vector<float>& s, double t0, double t1, double win, double& fmin, double& fmax)
{
    fmin = 1e9; fmax = 0.0;
    for (double t = t0; t + win <= t1; t += win * 0.5) {
        const double f = measureFreq(s, t, t + win);
        fmin = std::min(fmin, f);
        fmax = std::max(fmax, f);
    }
}

static void genericTests(const char* name)
{
    const double c = Engine::kSpeedOfSound;
    const bool stream = gEngine == dopplerit::kEngineStream;
    const double pitchTol = stream ? 0.01 : 0.004;
    std::printf("\n===== engine %s =====\n", name);

    {
        // tau must satisfy c tau = |emission position|
        const double v = 30.0, d = 5.0;
        for (double a : { -500.0, -20.0, 0.0, 15.0, 400.0 }) {
            const double tau = Engine::propagationTime(a, v, d);
            const double xe = a - v * tau;
            const double err = std::fabs(c * tau - std::sqrt(xe * xe + d * d));
            CHECK(err < 1e-9, "a=%.0f m: c*tau matches emission distance (err %.2e)", a, err);
        }
    }

    std::printf("[pitch: pass-by at 100 km/h, 1 kHz]\n");
    {
        Params p = defaults();
        p.period = 8.f;
        Render r = render(1, p, 8.0, 1000.0, noHook);
        const double v = 100.0 / 3.6;
        // early: source ~ -100 m away, approaching; late: receding
        const double up = measureFreq(r.l, 1.0, 1.5);
        const double down = measureFreq(r.l, 6.5, 7.0);
        // expected with the actual geometry (source far, cos ~ 0.998)
        const double expUp = 1000.0 * c / (c - v);
        const double expDown = 1000.0 * c / (c + v);
        std::printf("         approaching %.1f Hz (theory <= %.1f), receding %.1f Hz (theory >= %.1f)\n",
                    up, expUp, down, expDown);
        CHECK(std::fabs(up - expUp) / expUp < pitchTol, "approach pitch within %.1f %% of c/(c-v)", pitchTol * 100);
        CHECK(std::fabs(down - expDown) / expDown < pitchTol, "recede pitch within %.1f %% of c/(c+v)", pitchTol * 100);
        const double mid = measureFreq(r.l, 3.98, 4.02);
        CHECK(std::fabs(mid - 1000.0) < 25.0, "pitch ~unchanged at closest point (%.1f Hz)", mid);
    }

    std::printf("[modes]\n");
    {
        Params p = defaults();
        p.mode = dopplerit::kModeApproach;
        Render r = render(1, p, 3.5, 1000.0, noHook);
        const double f = measureFreq(r.l, 0.5, 2.0);
        CHECK(f > 1050.0, "approach mode only raises pitch (%.1f Hz)", f);
        p.mode = dopplerit::kModeRecede;
        r = render(1, p, 3.5, 1000.0, noHook);
        const double g = measureFreq(r.l, 2.0, 3.5);
        CHECK(g < 950.0, "recede mode only lowers pitch (%.1f Hz)", g);
    }

    std::printf("[clicks: loops, triggers, mode and parameter changes]\n");
    {
        Params p = defaults();
        p.period = 1.f;
        p.attenuation = 0.f; // worst case: no distance fade at the loop point
        p.speedKmh = 300.f;
        p.width = 1.f;
        auto hook = [](uint32_t pos, Params& pp, Engine& e, bool& en) {
            const double t = pos / kFs;
            if (t > 2.0 && t < 2.01) e.trigger();
            if (t > 3.0) pp.mode = dopplerit::kModeApproach;
            if (t > 4.0) pp.mode = dopplerit::kModeRecede;
            if (t > 5.0) { pp.loop = false; }
            if (t > 6.5) e.trigger();
            if (t > 7.0) { pp.speedKmh = 5.f; pp.distance = 50.f; pp.period = 10.f; }
            if (t > 8.0) en = false;
            if (t > 8.5) { en = true; pp.loop = true; }
            if (t > 9.0) pp.stereo = false;
            if (t > 9.5) pp.stereo = true;
            if (t > 10.0) { pp.mode = dopplerit::kModeOrbit; pp.speedKmh = 60.f; pp.period = 0.7f; pp.distance = 2.f; }
            if (t > 11.0) pp.mode = dopplerit::kModeSwing;
            if (t > 12.0) pp.engine = 1 - pp.engine;
        };
        Render r = render(2, p, 13.0, 220.0, hook);
        CHECK(finite(r.l) && finite(r.r), "output is finite");
        // A 220 Hz sine at 0.5 shifted up to 1.32x: max natural step ~0.019.
        // Equal-power cross-fades of uncorrelated heads stay well below 0.1.
        const float ml = maxStep(r.l, 4800), mr = maxStep(r.r, 4800);
        CHECK(ml < 0.06f && mr < 0.06f, "max sample step L=%.4f R=%.4f (no discontinuity)", ml, mr);
    }

    std::printf("[output mode mono]\n");
    {
        Params p = defaults();
        p.stereo = false;
        p.width = 1.f; // ignored in mono
        p.attenuation = 0.6f;
        Render r = render(2, p, 3.0, 440.0, noHook);
        float diff = 0.f;
        for (size_t i = 0; i < r.l.size(); ++i) diff = std::max(diff, std::fabs(r.l[i] - r.r[i]));
        CHECK(diff == 0.f, "mono output gives identical L and R (max diff %g)", diff);

        Render m = render(1, p, 3.0, 440.0, noHook);
        float dm = 0.f;
        for (size_t i = 0; i < m.l.size(); ++i) dm = std::max(dm, std::fabs(r.l[i] - m.l[i]));
        CHECK(dm < 1e-6f, "mono output equals a single-ear engine (max diff %g)", dm);

        p.stereo = true;
        p.width = 0.f;
        r = render(2, p, 3.0, 440.0, noHook);
        diff = 0.f;
        for (size_t i = 0; i < r.l.size(); ++i) diff = std::max(diff, std::fabs(r.l[i] - r.r[i]));
        CHECK(diff == 0.f, "stereo output with width 0 %% is mono compatible (max diff %g)", diff);
    }

    std::printf("[stereo offset]\n");
    {
        Params p = defaults();
        p.width = 1.f;
        p.attenuation = 0.f;
        Render r = render(2, p, 4.0, 1000.0, noHook);
        float diff = 0.f;
        for (size_t i = 0; i < r.l.size(); ++i) diff = std::max(diff, std::fabs(r.l[i] - r.r[i]));
        CHECK(diff > 0.1f, "width 100 %% decorrelates L and R");
    }

    std::printf("[bypass]\n");
    {
        Params p = defaults();
        auto hook = [](uint32_t, Params&, Engine&, bool& en) { en = false; };
        Engine* e = nullptr;
        Render r = render(2, p, 1.0, 440.0, hook, &e);
        // after the fade the output equals the input sine
        double ph = 0.0;
        float diff = 0.f;
        for (size_t i = 0; i < r.l.size(); ++i) {
            const float x = (float)(0.5 * std::sin(ph));
            ph += 2.0 * M_PI * 440.0 / kFs;
            if (i > kFs / 2) diff = std::max(diff, std::fabs(r.l[i] - x));
        }
        CHECK(diff < 1e-4f, "bypassed output equals input (max diff %g)", diff);
        delete e;
    }

    std::printf("[memory and CPU]\n");
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 }) {
            Engine e;
            e.init(fs, 2);
            std::printf("         %.0f Hz: delay line %u samples (%.1f s, %.1f MiB)\n", fs,
                        e.bufferSize(), e.bufferSize() / fs, e.bufferSize() * 4.0 / 1048576.0);
        }
        Params p = defaults();
        p.width = 0.5f;
        p.attenuation = 0.6f;
        p.period = 0.5f; // many cross-fades
        const auto t0 = std::chrono::steady_clock::now();
        Render r = render(2, p, 60.0, 440.0, noHook);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("         60 s of stereo audio rendered in %.3f s (%.2f %% of real time)\n", el, el / 60.0 * 100.0);
        CHECK(finite(r.l), "long render is finite");
    }
}

int main()
{
    gEngine = dopplerit::kEngineTape;
    genericTests("Tape");
    gEngine = dopplerit::kEngineStream;
    genericTests("Stream");

    const double c = Engine::kSpeedOfSound;
    std::printf("\n===== Stream specific =====\n");

    std::printf("[latency stays bounded]\n");
    for (int mode : { dopplerit::kModeApproach, dopplerit::kModeRecede, dopplerit::kModePassBy,
                      dopplerit::kModeOrbit, dopplerit::kModeSwing }) {
        for (int eng : { dopplerit::kEngineTape, dopplerit::kEngineStream }) {
            Params p = defaults();
            p.engine = eng;
            p.mode = mode;
            p.speedKmh = 300.f;
            p.period = 10.f;
            p.width = 1.f;
            Engine* e = nullptr;
            render(2, p, 12.0, 220.0, noHook, &e);
            const double lat = e->maxReadDelay();
            if (eng == dopplerit::kEngineStream)
                CHECK(lat < 0.045, "mode %d: Stream max latency %.1f ms (%u splices)", mode, lat * 1000.0, e->spliceCount());
            else
                std::printf("         mode %d: Tape max latency %.0f ms\n", mode, lat * 1000.0);
            delete e;
        }
    }

    std::printf("[orbit: pitch swings between c/(c+v) and c/(c-v)]\n");
    for (int eng : { dopplerit::kEngineTape, dopplerit::kEngineStream }) {
        Params p = defaults();
        p.engine = eng;
        p.mode = dopplerit::kModeOrbit;
        p.speedKmh = 72.f;          // 20 m/s
        p.period = 1.f;             // radius 3.2 m
        p.distance = 10.f;          // listener outside the orbit
        Render r = render(1, p, 4.0, 1000.0, noHook);
        double fmin, fmax;
        freqRange(r.l, 1.0, 4.0, 0.03, fmin, fmax);
        const double v = 20.0;
        const double hi = 1000.0 * c / (c - v), lo = 1000.0 * c / (c + v);
        std::printf("         %s: %.1f .. %.1f Hz (theory %.1f .. %.1f)\n", eng ? "Stream" : "Tape", fmin, fmax, lo, hi);
        CHECK(std::fabs(fmax - hi) / hi < 0.01 && std::fabs(fmin - lo) / lo < 0.01, "%s orbit pitch range matches physics",
              eng ? "Stream" : "Tape");
        // periodic: same pitch one revolution later
        const double f1 = measureFreq(r.l, 1.20, 1.25), f2 = measureFreq(r.l, 2.20, 2.25);
        CHECK(std::fabs(f1 - f2) < 3.0, "%s orbit is periodic (%.1f vs %.1f Hz)", eng ? "Stream" : "Tape", f1, f2);
    }

    std::printf("[swing: pitch oscillates, bounded by c/(c-v)]\n");
    {
        Params p = defaults();
        p.engine = dopplerit::kEngineStream;
        p.mode = dopplerit::kModeSwing;
        p.speedKmh = 72.f;
        p.period = 2.f;
        p.distance = 3.f;
        Render r = render(1, p, 6.0, 1000.0, noHook);
        double fmin, fmax;
        freqRange(r.l, 1.0, 6.0, 0.03, fmin, fmax);
        const double v = 20.0;
        std::printf("         %.1f .. %.1f Hz\n", fmin, fmax);
        CHECK(fmax > 1020.0 && fmin < 980.0, "swing modulates the pitch both ways");
        CHECK(fmax < 1000.0 * c / (c - v) * 1.01 && fmin > 1000.0 * c / (c + v) * 0.99, "swing stays within physical bounds");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
