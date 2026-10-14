// Render every factory preset on the test signal (build/presets/*.wav) and
// print its repeats, transposition range, peak level and CPU load.
//
//   make presets-demo

#include "presets.hpp"
#include "demo_signal.hpp"

#include <chrono>

using namespace dopplerit;

static AgeParams toParams(const Preset& pr)
{
    AgeParams p;
    p.mode = pr.mode;
    p.time = pr.time;
    p.speedKmh = pr.speedKmh;
    p.depth = pr.depth;
    p.shape = pr.shape * 0.01f;
    p.period = pr.period;
    p.feedback = pr.feedback * 0.01f;
    p.tone = pr.tone * 0.01f;
    p.mix = pr.mix * 0.01f;
    p.stereo = true;
    return p;
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::vector<float> in = testSignal();
    for (int i = 0; i < kNumPresets; ++i) {
        const Preset& pr = kPresets[i];
        const AgeParams p = toParams(pr);
        AgeDelayEngine e;
        e.init(kFs, 2);
        e.reset(p);
        std::vector<float> l(in.size()), r(in.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos < in.size(); pos += 128) {
            const uint32_t n = (uint32_t)std::min<size_t>(128, in.size() - pos);
            float* outs[2] = { &l[pos], &r[pos] };
            e.process(&in[pos], outs, n, p, true);
        }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
                         / ((double)in.size() / kFs) * 100.0;
        float peak = 0.f;
        for (size_t k = 0; k < l.size(); ++k) peak = std::max(peak, std::max(std::fabs(l[k]), std::fabs(r[k])));
        char name[128];
        std::snprintf(name, sizeof(name), "%s/%02d_%s.wav", dir.c_str(), i + 1, pr.id);
        writeWav(name, l, r);
        const double hi = 12.0 * std::log2(e.pitchAtAge(pr.time)), lo = 12.0 * std::log2(e.pitchAtAge(pr.time * e.taps()));
        std::printf("%02d %-22s %2d repeats  %+5.1f .. %+5.1f st  peak %.2f  cpu %.1f %%\n",
                    i + 1, pr.name, e.taps(), hi, lo, peak, cpu);
    }
    return 0;
}
