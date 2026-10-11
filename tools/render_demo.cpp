// Render demo WAV files of the DopplerIt delay engine from a synthetic test
// signal (detached plucked notes, then a short phrase, with silences so the
// repeats can be heard drifting).
//
//   make demo        (writes build/demo/*.wav)

#include "delay_engine.hpp"
#include "demo_signal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using dopplerit::DelayEngine;
using dopplerit::DelayParams;


// triggers: times (s) at which the trigger button is pressed
static void render(const std::string& dir, const char* name, const std::vector<float>& in, DelayParams p,
                   std::vector<double> triggers = {})
{
    DelayEngine e;
    e.init(kFs, 2);
    e.reset(p);
    std::vector<float> l(in.size()), r(in.size());
    const uint32_t block = 128;
    size_t next = 0;
    for (size_t pos = 0; pos < in.size(); pos += block) {
        if (next < triggers.size() && pos / kFs >= triggers[next]) {
            e.trigger();
            ++next;
        }
        const uint32_t n = (uint32_t)std::min<size_t>(block, in.size() - pos);
        float* outs[2] = { &l[pos], &r[pos] };
        e.process(&in[pos], outs, n, p, true);
    }
    float peak = 0.f;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    writeWav(dir + "/" + name, l, r);
    std::printf("%-30s peak %.2f\n", name, peak);
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::vector<float> in = testSignal();
    writeWav(dir + "/00_dry.wav", in, in);

    DelayParams base;
    base.mode = dopplerit::kModePassBy;
    base.heads = 1;
    base.time = 0.25f;
    base.speedKmh = 100.f;
    base.distance = 5.f;
    base.period = 2.5f;
    base.stagger = 0.f;
    base.feedback = 0.35f;
    base.tone = 0.6f;
    base.mix = 0.5f;
    base.stereo = true;
    base.loop = true;

    DelayParams p = base;
    render(dir, "01_heads1.wav", in, p);
    p.heads = 2;
    render(dir, "02_heads2_sync.wav", in, p);
    p.heads = 3;
    render(dir, "03_heads3_sync.wav", in, p);
    p.heads = 4;
    render(dir, "04_heads4_sync.wav", in, p);
    p.stagger = 0.25f;
    render(dir, "05_heads4_staggered.wav", in, p);

    p = base;
    p.heads = 2;
    p.stagger = 0.5f;
    render(dir, "06_heads2_staggered.wav", in, p);

    p = base;
    p.heads = 3;
    p.loop = false;
    p.feedback = 0.45f;
    render(dir, "07_trig_oneshot_heads3.wav", in, p, { 0.3, 6.4, 10.9 });

    p = base;
    p.distance = 1.f;
    p.speedKmh = 150.f;
    render(dir, "08_sharp_pass_1m_150kmh.wav", in, p);

    p = base;
    p.heads = 2;
    p.distance = 40.f;
    p.speedKmh = 80.f;
    p.period = 4.f;
    render(dir, "09_gentle_pass_40m.wav", in, p);

    p = base;
    p.time = 0.12f;
    p.period = 1.2f;
    p.speedKmh = 120.f;
    p.feedback = 0.5f;
    render(dir, "10_fast_succession.wav", in, p);

    p = base;
    p.heads = 2;
    p.distance = 10.f;
    p.mode = dopplerit::kModeApproach;
    render(dir, "11_approach_heads2.wav", in, p);
    p.mode = dopplerit::kModeRecede;
    render(dir, "12_recede_heads2.wav", in, p);
    return 0;
}
