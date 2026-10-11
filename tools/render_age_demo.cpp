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
    std::printf("%-34s %2d repeats in the pass, %4u splices, peak %.2f\n   semitones:", name, e.taps(), e.splices(), peak);
    for (int k = 0; k < e.taps(); ++k)
        std::printf(" %+.1f", 12.0 * std::log2(e.pitchAtAge((k + 1) * (double)p.time)));
    std::printf("\n");
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
    base.shape = 0.5f;
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
    p.stereo = false;
    render(dir, "age_04_passby_mono.wav", in, p);

    // one octave each way, same pass (2 s, 8 repeats), from a quick swap to
    // a glide over the whole pass
    p = base;
    p.speedKmh = 300.f;
    p.depth = 8.f;
    p.shape = 0.05f;
    render(dir, "age_05_octave_shape05.wav", in, p);
    p.shape = 0.3f;
    render(dir, "age_06_octave_shape30.wav", in, p);
    p.shape = 0.6f;
    render(dir, "age_07_octave_shape60.wav", in, p);
    p.shape = 1.f;
    render(dir, "age_08_octave_shape100.wav", in, p);

    // dense repeats: the staircase becomes a glide
    p = base;
    p.speedKmh = 300.f;
    p.depth = 8.f;
    p.shape = 0.6f;
    p.time = 0.1f;
    p.period = 2.4f;
    p.feedback = 0.75f;
    render(dir, "age_09_octave_dense_shape60.wav", in, p);
    p.shape = 1.f;
    render(dir, "age_10_octave_dense_shape100.wav", in, p);

    // long pass, dense, one octave each way
    p = base;
    p.time = 0.25f;
    p.period = 6.f;
    p.shape = 0.7f;
    p.speedKmh = 300.f;
    p.depth = 8.f;
    p.feedback = 0.65f;
    render(dir, "age_11_octave_long_6s.wav", in, p);
    return 0;
}
