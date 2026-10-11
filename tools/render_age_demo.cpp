// Render demo WAV files of the "age" engine (time-invariant Doppler delay).
//
//   make demo        (writes build/demo/age_*.wav)

#include "age_engine.hpp"
#include "demo_signal.hpp"

using dopplerit::AgeDelayEngine;
using dopplerit::AgeParams;

static void render(const std::string& dir, const char* name, const std::vector<float>& in, AgeParams p)
{
    AgeDelayEngine e;
    e.init(kFs, 2);
    e.reset(p);
    std::vector<float> l(in.size()), r(in.size());
    for (size_t pos = 0; pos < in.size(); pos += 128) {
        const uint32_t n = (uint32_t)std::min<size_t>(128, in.size() - pos);
        float* outs[2] = { &l[pos], &r[pos] };
        e.process(&in[pos], outs, n, p, true);
    }
    float peak = 0.f;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    writeWav(dir + "/" + name, l, r);
    std::printf("%-34s %2d repeats in the pass, %4u splices, peak %.2f\n", name, e.taps(), e.splices(), peak);
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::vector<float> in = testSignal();
    writeWav(dir + "/age_00_dry.wav", in, in);

    AgeParams base;
    base.mode = dopplerit::kAgePassBy;
    base.time = 0.25f;
    base.speedKmh = 100.f;
    base.depth = 1.f;
    base.distance = 5.f;
    base.period = 2.f;
    base.feedback = 0.7f;
    base.tone = 0.6f;
    base.mix = 0.5f;
    base.stereo = true;

    AgeParams p = base;
    render(dir, "age_01_passby.wav", in, p);
    p.mode = dopplerit::kAgeApproach;
    render(dir, "age_02_approach.wav", in, p);
    p.mode = dopplerit::kAgeRecede;
    render(dir, "age_03_recede.wav", in, p);

    p = base;
    p.time = 0.12f;
    p.period = 0.8f;
    p.speedKmh = 150.f;
    render(dir, "age_04_short_fast_pass.wav", in, p);

    p = base;
    p.time = 0.4f;
    p.period = 4.f;
    p.speedKmh = 120.f;
    p.distance = 20.f;
    p.feedback = 0.75f;
    render(dir, "age_05_long_gentle_pass.wav", in, p);

    p = base;
    p.distance = 1.f;
    p.speedKmh = 200.f;
    render(dir, "age_06_sharp_pass_200kmh.wav", in, p);

    p = base;
    p.stereo = false;
    render(dir, "age_07_passby_mono.wav", in, p);

    // exaggerated settings
    p = base;
    p.speedKmh = 300.f;
    p.depth = 8.f;
    render(dir, "age_08_octave_passby.wav", in, p);
    p.distance = 1.f;
    render(dir, "age_09_octave_sharp.wav", in, p);

    p = base;
    p.time = 0.1f;
    p.period = 2.4f;
    p.depth = 4.f;
    p.speedKmh = 150.f;
    p.feedback = 0.7f;
    render(dir, "age_10_dense_24_repeats.wav", in, p);

    p = base;
    p.time = 0.5f;
    p.period = 12.f;
    p.distance = 30.f;
    p.speedKmh = 200.f;
    p.depth = 5.f;
    p.feedback = 0.9f;
    render(dir, "age_11_long_12s_pass.wav", in, p);
    return 0;
}
