/*
 * DopplerIt - Doppler effect LV2 plugin for MOD devices and Raspberry Pi 5
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Physical model
 * --------------
 * A virtual source moves in front of the listener. The listener's ear(s)
 * sit on the x axis at y = 0 (mono: one ear at x = 0, stereo: two ears at
 * x = -e and x = +e, e being driven by the "Width" control).
 *
 * Trajectories:
 *   - Approach / Recede / Pass-by: straight line y = d at constant speed v.
 *     The propagation time tau (signal heard at t was emitted at t - tau)
 *     has a closed form:
 *         c^2 tau^2 = (a - v tau)^2 + d^2      (a = source x minus ear x)
 *      => tau = (-a v + sqrt(a^2 v^2 + (c^2 - v^2)(a^2 + d^2))) / (c^2 - v^2)
 *   - Orbit: circle of radius R = v T / 2pi centred at distance d (like the
 *     horn of a rotating speaker, seen from outside).
 *   - Swing: back and forth along the line y = d, amplitude A = v T / 2pi
 *     (peak speed v).
 *   For Orbit and Swing, tau = |p(t - tau) - ear| / c is solved by fixed-point
 *   iteration, which converges because the source is slower than sound.
 *
 * Read engines
 * ------------
 *   - Tape: the delay line is read with the full physical delay. Exact, but
 *     a far source is heard seconds late and every new pass jumps back in
 *     time (the "replayed sample" character).
 *   - Stream: the read head follows the *variation* of the physical delay
 *     (hence the exact same Doppler pitch), but stays within a short window
 *     (kStreamMin .. kStreamMin + kStreamWindow). When it leaves the window
 *     it is moved by about one window length with a short cross-fade, at the
 *     position where the waveforms match best (normalized cross-correlation).
 *     Latency stays around 6..26 ms whatever the trajectory: the output
 *     follows the input continuously, like a delay or a reverb.
 *
 * Distance also drives an optional 1/r attenuation and an air-absorption
 * low-pass filter. Restarting a cycle (loop, trigger, mode change, end of a
 * one-shot pass) cross-fades two "voices" so there is never a click.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace dopplerit {

enum Mode : int {
    kModeApproach = 0, // source comes from far away towards the listener
    kModeRecede   = 1, // source moves away from the listener
    kModePassBy   = 2, // full fly-by: approach then recede
    kModeOrbit    = 3, // circular orbit (rotating speaker)
    kModeSwing    = 4, // back and forth in front of the listener
};

enum ReadEngine : int {
    kEngineTape   = 0, // full physical delay
    kEngineStream = 1, // bounded latency, continuous stream
};

struct Params {
    int   mode;        // Mode
    int   engine;      // ReadEngine
    float speedKmh;    // source speed in km/h
    float period;      // duration of one pass / revolution in seconds
    float distance;    // closest distance (or orbit centre distance) in meters
    float attenuation; // 0..1, amount of distance attenuation + air absorption
    float width;       // 0..1, stereo spread (ignored in mono)
    bool  stereo;      // output mode: false = same mono signal on every output
    float mix;         // 0..1, dry/wet balance
    bool  loop;        // repeat passes continuously
};

// Parameter ranges (must match the .ttl files)
static constexpr float kMinSpeedKmh  = 5.0f;
static constexpr float kMaxSpeedKmh  = 300.0f;
static constexpr float kMinPeriod    = 0.1f;
static constexpr float kMaxPeriod    = 16.0f;
static constexpr float kMinDistance  = 1.0f;
static constexpr float kMaxDistance  = 50.0f;

class Engine {
public:
    static constexpr double kPi           = 3.14159265358979323846;
    static constexpr double kTwoPi        = 2.0 * kPi;
    static constexpr double kSpeedOfSound = 343.0;  // m/s
    static constexpr double kMaxEarOffset = 2.0;    // m, at width = 100 %
    static constexpr float  kMaxPanDepth  = 0.7f;   // at width = 100 %
    static constexpr double kFadeTime     = 0.050;  // s, voice cross-fade
    static constexpr double kMinOrbitDist = 0.25;   // m, closest allowed approach
    static constexpr int    kMaxChannels  = 2;

    // Stream engine
    static constexpr double kStreamMin    = 0.006;  // s, shortest read delay
    static constexpr double kStreamWindow = 0.020;  // s, splice length
    static constexpr double kSpliceFade   = 0.010;  // s, splice cross-fade
    static constexpr double kSpliceSearch = 0.004;  // s, +/- correlation search
    static constexpr double kCorrLength   = 0.004;  // s, correlation window

    Engine() = default;
    ~Engine() { release(); }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool init(double sampleRate, int channels)
    {
        fs_       = sampleRate;
        channels_ = channels < 1 ? 1 : (channels > kMaxChannels ? kMaxChannels : channels);

        // Worst case delay: Tape engine, approach mode, max speed, max period.
        const double vmax = kMaxSpeedKmh / 3.6;
        const double L    = vmax * kMaxPeriod + kMaxEarOffset + 1.0;
        const double tau  = propagationTime(-L, vmax, kMinDistance) - kMinDistance / kSpeedOfSound;
        const double need = tau * fs_ + 16.0;

        uint32_t size = 1;
        while ((double)size < need)
            size <<= 1;

        bufSize_ = size;
        bufMask_ = size - 1;
        buf_     = static_cast<float*>(std::calloc(size, sizeof(float)));

        fadeLen_ = (uint32_t)(kFadeTime * fs_);
        if (fadeLen_ < 16)
            fadeLen_ = 16;
        fadeIn_  = static_cast<float*>(std::malloc((fadeLen_ + 1) * sizeof(float)));

        spliceLen_ = (uint32_t)(kSpliceFade * fs_);
        if (spliceLen_ < 8)
            spliceLen_ = 8;
        spliceIn_  = static_cast<float*>(std::malloc((spliceLen_ + 1) * sizeof(float)));

        if (buf_ == nullptr || fadeIn_ == nullptr || spliceIn_ == nullptr) {
            release();
            return false;
        }

        // equal-power fade-in curve, fade-out uses the mirrored table
        for (uint32_t i = 0; i <= fadeLen_; ++i)
            fadeIn_[i] = (float)std::sin(0.5 * kPi * (double)i / (double)fadeLen_);

        // raised cosine (sums to one) for splices of correlated material
        for (uint32_t i = 0; i <= spliceLen_; ++i)
            spliceIn_[i] = (float)(0.5 - 0.5 * std::cos(kPi * (double)i / (double)spliceLen_));

        corrHalf_   = (int)(0.5 * kCorrLength * fs_);
        searchHalf_ = (int)(kSpliceSearch * fs_);
        searchStep_ = fs_ > 60000.0 ? 2 : 1;

        smoothSlow_  = (float)(1.0 - std::exp(-1.0 / (0.150 * fs_)));
        smoothFast_  = (float)(1.0 - std::exp(-1.0 / (0.030 * fs_)));
        maxCutoff_   = std::fmin(18000.0, 0.45 * fs_);
        return true;
    }

    void release()
    {
        std::free(buf_);
        std::free(fadeIn_);
        std::free(spliceIn_);
        buf_      = nullptr;
        fadeIn_   = nullptr;
        spliceIn_ = nullptr;
    }

    // Call from activate() or whenever the stream restarts.
    void reset(const Params& p)
    {
        if (buf_ != nullptr)
            std::memset(buf_, 0, bufSize_ * sizeof(float));
        writePos_ = 0;

        speed_    = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        period_   = clampf(p.period, kMinPeriod, kMaxPeriod);
        distance_ = clampf(p.distance, kMinDistance, kMaxDistance);
        atten_    = clampf(p.attenuation, 0.f, 1.f);
        width_    = (channels_ > 1 && p.stereo) ? clampf(p.width, 0.f, 1.f) : 0.f;
        mix_      = clampf(p.mix, 0.f, 1.f);
        enabled_  = 1.f;
        ears_     = (channels_ > 1 && width_ > 0.f) ? channels_ : 1;
        engine_   = clampEngine(p.engine);

        cur_ = makeVoice(clampMode(p.mode), 0.0, true);
        placeReadHead(cur_, nullptr);
        old_ = cur_;
        fadePos_        = 0;
        fading_         = false;
        triggerPending_ = false;
        splices_        = 0;
        maxReadDelay_   = 0.0;
    }

    // Request a new pass from the start (footswitch / trigger button).
    void trigger() { triggerPending_ = true; }

    // in: mono input, out: channels_ output buffers. "enabled" is the LV2
    // enabled designation (0 = bypassed), it is smoothed to avoid clicks.
    void process(const float* in, float* const* out, uint32_t frames, const Params& p, bool enabled)
    {
        const float tSpeed  = clampf(p.speedKmh, kMinSpeedKmh, kMaxSpeedKmh) / 3.6f;
        const float tPeriod = clampf(p.period, kMinPeriod, kMaxPeriod);
        const float tDist   = clampf(p.distance, kMinDistance, kMaxDistance);
        const float tAtt    = clampf(p.attenuation, 0.f, 1.f);
        const float tWidth  = (channels_ > 1 && p.stereo) ? clampf(p.width, 0.f, 1.f) : 0.f;
        const float tMix    = clampf(p.mix, 0.f, 1.f);
        const float tEn     = enabled ? 1.f : 0.f;
        const int   tMode   = clampMode(p.mode);
        const int   tEngine = clampEngine(p.engine);

        float wet[kMaxChannels];
        float wetOld[kMaxChannels];

        for (uint32_t i = 0; i < frames; ++i)
        {
            // parameter smoothing
            speed_    += smoothSlow_ * (tSpeed  - speed_);
            period_   += smoothSlow_ * (tPeriod - period_);
            distance_ += smoothSlow_ * (tDist   - distance_);
            atten_    += smoothFast_ * (tAtt    - atten_);
            width_    += smoothFast_ * (tWidth  - width_);
            mix_      += smoothFast_ * (tMix    - mix_);
            enabled_  += smoothFast_ * (tEn     - enabled_);

            // Mono output: once the width has faded out both ears are
            // identical, so only one is computed and copied.
            if (tWidth <= 0.f && width_ < 1e-4f)
                width_ = 0.f;
            ears_ = (channels_ > 1 && width_ > 0.f) ? channels_ : 1;

            const float x = in[i];
            buf_[writePos_] = x;

            // voice management: only one transition at a time
            if (!fading_)
                updateVoiceState(tMode, tEngine, p.loop);

            renderVoice(cur_, wet);

            if (fading_)
            {
                renderVoice(old_, wetOld);
                float gIn  = fadeIn_[fadePos_];
                float gOut = fadeIn_[fadeLen_ - fadePos_];
                if (fadeCorrelated_) {
                    // both heads read (almost) the same audio: amplitude sum
                    gIn  *= gIn;
                    gOut  = 1.f - gIn;
                }
                for (int c = 0; c < ears_; ++c)
                    wet[c] = wet[c] * gIn + wetOld[c] * gOut;

                advanceVoice(old_);
                if (++fadePos_ > fadeLen_)
                    fading_ = false;
            }

            advanceVoice(cur_);

            if (ears_ < channels_)
            {
                wet[1] = wet[0];
                cur_.lp[1] = cur_.lp[0];
                old_.lp[1] = old_.lp[0];
            }

            const float dryG = mix_ < 0.5f ? 1.f : 2.f * (1.f - mix_);
            const float wetG = mix_ > 0.5f ? 1.f : 2.f * mix_;

            for (int c = 0; c < channels_; ++c)
            {
                const float processed = dryG * x + wetG * wet[c];
                out[c][i] = x + enabled_ * (processed - x);
            }

            writePos_ = (writePos_ + 1) & bufMask_;
        }

        // flush denormals in filter states
        for (int c = 0; c < kMaxChannels; ++c)
        {
            if (std::fabs(cur_.lp[c]) < 1e-20f) cur_.lp[c] = 0.f;
            if (std::fabs(old_.lp[c]) < 1e-20f) old_.lp[c] = 0.f;
        }
    }

    // Exact propagation time for a source currently at relative position a
    // (along a straight path), moving at speed v, at perpendicular distance d.
    static double propagationTime(double a, double v, double d)
    {
        const double c2  = kSpeedOfSound * kSpeedOfSound;
        const double c2v = c2 - v * v;
        return (-a * v + std::sqrt(a * a * v * v + c2v * (a * a + d * d))) / c2v;
    }

    uint32_t bufferSize() const { return bufSize_; }

    // diagnostics (tests, demos)
    uint32_t spliceCount() const { return splices_; }
    double   maxReadDelay() const { return maxReadDelay_; }
    void     clearStats() { splices_ = 0; maxReadDelay_ = 0.0; }

private:
    struct Voice {
        int    mode;
        bool   moving;    // false once a one-shot pass is over
        double x;         // straight paths: source position along the path (m)
        double phi;       // orbit / swing: phase (rad)
        double tauPrev[kMaxChannels]; // warm start of the iterative solver (< 0: none)
        float  lp[kMaxChannels];      // air absorption filter states
        // stream engine
        double offset;     // s, subtracted from the physical delay
        double prevOffset; // s, previous offset while splicing
        uint32_t splicePos;
        bool   splicing;
    };

    struct Geo {
        double delay; // s, physical delay minus the closest approach time
        float  k;     // closest distance / distance (1 = closest, -> 0 far away)
        float  sinT;  // lateral direction of the source (-1 left .. 1 right)
    };

    static float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    static int clampMode(int m)
    {
        return m < kModeApproach ? kModeApproach : (m > kModeSwing ? kModeSwing : m);
    }

    static int clampEngine(int e)
    {
        return e <= kEngineTape ? kEngineTape : kEngineStream;
    }

    static bool isPeriodic(int mode) { return mode == kModeOrbit || mode == kModeSwing; }

    double pathLength() const { return (double)speed_ * (double)period_; }
    double omega() const { return kTwoPi / (double)period_; }
    double radius() const { return (double)speed_ * (double)period_ / kTwoPi; } // orbit radius, swing amplitude

    static double xStart(int mode, double L)
    {
        switch (mode) {
        case kModeApproach: return -L;
        case kModeRecede:   return 0.0;
        default:            return -0.5 * L;
        }
    }

    static double xEnd(int mode, double L)
    {
        switch (mode) {
        case kModeApproach: return 0.0;
        case kModeRecede:   return L;
        default:            return 0.5 * L;
        }
    }

    // orbit starts at its farthest point, swing at one end (both at rest)
    static double phiStart(int mode) { return mode == kModeSwing ? -0.5 * kPi : 0.0; }

    // progress along the cycle, 0..1
    double progress(const Voice& v) const
    {
        double f;
        if (isPeriodic(v.mode)) {
            f = (v.phi - phiStart(v.mode)) / kTwoPi;
        } else {
            const double L = pathLength();
            const double s0 = xStart(v.mode, L), s1 = xEnd(v.mode, L);
            f = (s1 > s0) ? (v.x - s0) / (s1 - s0) : 0.0;
        }
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }

    Voice makeVoice(int mode, double frac, bool moving) const
    {
        Voice v;
        std::memset(&v, 0, sizeof(v));
        v.mode   = mode;
        v.moving = moving;
        if (isPeriodic(mode)) {
            v.phi = phiStart(mode) + frac * kTwoPi;
        } else {
            const double L = pathLength();
            v.x = xStart(mode, L) + frac * (xEnd(mode, L) - xStart(mode, L));
        }
        v.tauPrev[0] = v.tauPrev[1] = -1.0;
        return v;
    }

    double earPosition(int c) const
    {
        const double ear = (double)width_ * kMaxEarOffset;
        return ears_ == 1 ? 0.0 : (c == 0 ? -ear : ear);
    }

    // source position at phase phi (orbit / swing)
    void periodicPosition(int mode, double phi, double& px, double& py) const
    {
        const double R = radius();
        const double d = (double)distance_;
        if (mode == kModeOrbit) {
            px = R * std::sin(phi);
            py = d + R * std::cos(phi);
        } else {
            px = R * std::sin(phi);
            py = d;
        }
    }

    void geometry(Voice& vc, int c, Geo& g) const
    {
        const double earX = earPosition(c);
        const double d    = (double)distance_;

        if (!isPeriodic(vc.mode))
        {
            const double v   = vc.moving ? (double)speed_ : 0.0;
            const double a   = vc.x - earX;
            const double tau = propagationTime(a, v, d);
            const double r   = kSpeedOfSound * tau; // distance at emission time, >= d
            g.delay = tau - d / kSpeedOfSound;
            g.k     = (float)(d / r);
            g.sinT  = (float)((a - v * tau) / r);
            return;
        }

        const double w = vc.moving ? omega() : 0.0;
        double px, py;
        double tau = vc.tauPrev[c];
        if (tau < 0.0) {
            periodicPosition(vc.mode, vc.phi, px, py);
            tau = std::sqrt((px - earX) * (px - earX) + py * py) / kSpeedOfSound;
        }
        for (int it = 0; it < 3; ++it) {
            periodicPosition(vc.mode, vc.phi - w * tau, px, py);
            tau = std::sqrt((px - earX) * (px - earX) + py * py) / kSpeedOfSound;
        }
        vc.tauPrev[c] = tau;

        const double r    = kSpeedOfSound * tau;
        double       rmin = vc.mode == kModeOrbit ? std::fabs(d - radius()) : d;
        if (rmin < kMinOrbitDist)
            rmin = kMinOrbitDist;
        g.delay = tau - rmin / kSpeedOfSound;
        g.k     = (float)(r > rmin ? rmin / r : 1.0);
        g.sinT  = (float)((px - earX) / (r > 1e-6 ? r : 1e-6));
    }

    // Stream engine: choose the offset of a new voice so its read head starts
    // where "from" currently reads (continuity), or centred in the window.
    void placeReadHead(Voice& v, const Voice* from)
    {
        v.splicing = false;
        v.splicePos = 0;
        if (engine_ == kEngineTape) {
            v.offset = v.prevOffset = 0.0;
            return;
        }
        Geo g;
        geometry(v, 0, g);
        double target = kStreamMin + 0.5 * kStreamWindow;
        if (from != nullptr) {
            Voice tmp = *from;
            Geo gf;
            geometry(tmp, 0, gf);
            const double e = gf.delay - from->offset;
            if (e >= kStreamMin && e <= kStreamMin + kStreamWindow)
                target = e;
        }
        v.offset = v.prevOffset = g.delay - target;
    }

    void startFade(const Voice& next, bool correlated)
    {
        old_     = cur_;
        cur_     = next;
        // keep the filter memory so the new head does not start from silence
        cur_.lp[0] = old_.lp[0];
        cur_.lp[1] = old_.lp[1];
        placeReadHead(cur_, correlated ? &old_ : nullptr);
        fadeCorrelated_ = correlated && engine_ == kEngineStream;
        fadePos_ = 0;
        fading_  = true;
    }

    void updateVoiceState(int mode, int engine, bool loop)
    {
        const bool stream = engine == kEngineStream;

        if (engine != engine_)
        {
            engine_ = engine;
            Voice next = cur_;
            next.tauPrev[0] = next.tauPrev[1] = -1.0;
            startFade(next, false);
            return;
        }

        if (triggerPending_)
        {
            triggerPending_ = false;
            startFade(makeVoice(mode, 0.0, true), stream);
            return;
        }

        if (cur_.mode != mode)
        {
            // keep the same progress along the new path
            startFade(makeVoice(mode, progress(cur_), cur_.moving), stream);
            return;
        }

        if (cur_.moving)
        {
            if (isPeriodic(cur_.mode))
            {
                const double end = phiStart(cur_.mode) + kTwoPi;
                if (cur_.phi >= end)
                {
                    if (loop) // periodic: seamless wrap
                        cur_.phi -= kTwoPi;
                    else
                        startFade(makeVoice(mode, 1.0, false), stream);
                }
            }
            else if (cur_.x >= xEnd(cur_.mode, pathLength()))
            {
                if (loop)
                    startFade(makeVoice(mode, 0.0, true), stream);
                else // one-shot: the source stops at the end of its path
                    startFade(makeVoice(mode, 1.0, false), stream);
            }
        }
        else if (loop)
        {
            startFade(makeVoice(mode, 0.0, true), stream);
        }
    }

    void advanceVoice(Voice& v) const
    {
        if (!v.moving)
            return;
        if (isPeriodic(v.mode))
            v.phi += omega() / fs_;
        else
            v.x += (double)speed_ / fs_;
    }

    // normalized cross-correlation between the audio around two read delays
    // (in samples, integer)
    float correlation(uint32_t da, uint32_t db) const
    {
        float ab = 0.f, aa = 0.f, bb = 0.f;
        const uint32_t pa = writePos_ - da, pb = writePos_ - db;
        for (int k = -corrHalf_; k <= corrHalf_; k += 2) {
            const float a = buf_[(pa + (uint32_t)k) & bufMask_];
            const float b = buf_[(pb + (uint32_t)k) & bufMask_];
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        return ab / std::sqrt(aa * bb + 1e-12f);
    }

    // Stream engine: move the read head when it leaves the window
    void maybeSplice(Voice& vc, const double* phys)
    {
        if (vc.splicing)
            return;

        double eLo = 1e9, eHi = -1e9;
        for (int c = 0; c < ears_; ++c) {
            const double e = phys[c] - vc.offset;
            eLo = e < eLo ? e : eLo;
            eHi = e > eHi ? e : eHi;
        }
        const double span = eHi - eLo;

        // Nominal jump leaves room for the correlation search, so the head
        // always lands inside the window (no immediate splice back).
        const double lo = kStreamMin, hi = kStreamMin + kStreamWindow + span;
        double shift;
        if (eLo < lo)
            shift = kStreamWindow - kSpliceSearch;    // pitch up: read again a bit of the past
        else if (eHi > hi)
            shift = -(kStreamWindow - kSpliceSearch); // pitch down: skip a bit forward
        else
            return;

        // best matching position around the nominal splice
        const double e0  = phys[0] - vc.offset;
        const double lim = (double)(corrHalf_ + 4) / fs_;
        const uint32_t from = (uint32_t)(e0 * fs_ + 2.0);
        double best = shift;
        float  bestScore = -2.f;
        for (int s = -searchHalf_; s <= searchHalf_; s += searchStep_) {
            const double cand = shift + (double)s / fs_;
            if (eLo + cand < lo || eHi + cand > hi)
                continue;
            if (eLo + cand < lim || eHi + cand > (double)(bufSize_ - 8) / fs_)
                continue;
            const float score = correlation(from, (uint32_t)((e0 + cand) * fs_ + 2.0));
            if (score > bestScore) {
                bestScore = score;
                best = cand;
            }
        }

        vc.prevOffset = vc.offset;
        vc.offset    -= best;
        vc.splicePos  = 0;
        vc.splicing   = true;
        ++splices_;
    }

    void renderVoice(Voice& vc, float* outs)
    {
        const float pan    = width_ * kMaxPanDepth;
        const bool  stream = engine_ == kEngineStream;

        Geo geo[kMaxChannels];
        double phys[kMaxChannels];
        for (int c = 0; c < ears_; ++c) {
            geometry(vc, c, geo[c]);
            phys[c] = geo[c].delay;
        }

        if (stream)
            maybeSplice(vc, phys);

        const float spliceGain = vc.splicing ? spliceIn_[vc.splicePos] : 1.f;

        for (int c = 0; c < ears_; ++c)
        {
            float s = readDelay(phys[c] - vc.offset);
            if (vc.splicing)
                s = s * spliceGain + readDelay(phys[c] - vc.prevOffset) * (1.f - spliceGain);

            const float k = geo[c].k;

            // air absorption: cutoff decreases with distance
            const double fc    = maxCutoff_ * std::sqrt((double)k);
            const float  alpha = (float)(1.0 - std::exp(-2.0 * kPi * fc / fs_));
            vc.lp[c] += alpha * (s - vc.lp[c]);
            s += atten_ * (vc.lp[c] - s);

            // distance attenuation (1/r law, normalized at the closest point)
            float g = 1.f - atten_ + atten_ * k;

            // stereo: attenuate the ear opposite to the source
            if (ears_ > 1)
            {
                const float sinT = geo[c].sinT;
                if (c == 0)
                    g *= 1.f - pan * (sinT > 0.f ? sinT : 0.f);
                else
                    g *= 1.f - pan * (sinT < 0.f ? -sinT : 0.f);
            }

            outs[c] = s * g;
        }

        if (vc.splicing && ++vc.splicePos > spliceLen_)
            vc.splicing = false;
    }

    float readDelay(double seconds)
    {
        double delay = seconds * fs_ + 2.0;
        const double maxDelay = (double)(bufSize_ - 4);
        if (delay > maxDelay) delay = maxDelay;
        if (delay < 2.0)      delay = 2.0;
        if (delay > maxReadDelay_ * fs_)
            maxReadDelay_ = delay / fs_;
        return readHermite(delay);
    }

    float readHermite(double delay) const
    {
        const uint32_t di   = (uint32_t)delay;
        const float    t    = (float)(delay - (double)di);
        const uint32_t base = writePos_ - di;

        const float p0 = buf_[(base + 1) & bufMask_];
        const float p1 = buf_[base & bufMask_];
        const float p2 = buf_[(base - 1) & bufMask_];
        const float p3 = buf_[(base - 2) & bufMask_];

        const float c1 = 0.5f * (p2 - p0);
        const float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
        const float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
        return ((c3 * t + c2) * t + c1) * t + p1;
    }

    double   fs_       = 48000.0;
    int      channels_ = 1;
    int      ears_     = 1; // ears actually computed (1 in mono output mode)
    int      engine_   = kEngineStream;

    float*   buf_      = nullptr;
    uint32_t bufSize_  = 0;
    uint32_t bufMask_  = 0;
    uint32_t writePos_ = 0;

    float*   fadeIn_   = nullptr;
    uint32_t fadeLen_  = 0;
    uint32_t fadePos_  = 0;
    bool     fading_   = false;
    bool     fadeCorrelated_ = false;
    bool     triggerPending_ = false;

    float*   spliceIn_   = nullptr;
    uint32_t spliceLen_  = 0;
    int      corrHalf_   = 96;
    int      searchHalf_ = 192;
    int      searchStep_ = 1;

    Voice    cur_ {};
    Voice    old_ {};

    // smoothed parameters
    float speed_ = 0.f, period_ = 1.f, distance_ = 1.f;
    float atten_ = 0.f, width_ = 0.f, mix_ = 0.f, enabled_ = 1.f;

    float  smoothSlow_ = 0.f;
    float  smoothFast_ = 0.f;
    double maxCutoff_  = 18000.0;

    // diagnostics
    uint32_t splices_      = 0;
    double   maxReadDelay_ = 0.0;
};

} // namespace dopplerit
