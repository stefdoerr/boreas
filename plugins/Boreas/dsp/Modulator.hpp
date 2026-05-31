#pragma once
#include <cmath>
#include "Constants.hpp"

namespace boreas {

// Per-layer "Movement" modulator: organic amplitude breathing + a subtle pitch
// shimmer, so the (otherwise dead-steady) frozen tone feels alive. The amplitude
// LFO is the sum of two incommensurate parabolic sines — quasi-periodic, so it
// breathes rather than ticking like a metronomic tremolo; the pitch LFO is a
// third, slower one. Each layer seeds its own phases and a small rate jitter, so
// stacked layers drift apart and shimmer independently. Depth = 0 returns exactly
// {1, 1} (true bypass: the frozen tone is then bit-identical to no modulation).
class Modulator {
public:
    struct Out { float amp; float pitch; };

    void prepare(double fs) { fs_ = fs; reset(0); }

    void reset(int seed) {
        p1_ = frac(0.10f + 0.374f * (float)seed);
        p2_ = frac(0.55f + 0.612f * (float)seed);
        p3_ = frac(0.30f + 0.137f * (float)seed);
        const float j = ((float)(seed % kSeeds) - (kSeeds - 1) * 0.5f) / ((kSeeds - 1) * 0.5f); // [-1,1]
        rateJit_ = 1.0f + 0.06f * j;     // ±6% per-layer rate spread -> layers decorrelate
        updateIncs();
    }

    void setRate(double r)  { baseHz_ = (float)moveRateToHz(r); updateIncs(); }
    void setDepth(double d) { depth_ = (d < 0.0) ? 0.0f : (d > 1.0 ? 1.0f : (float)d); }

    Out step() {
        if (depth_ <= 0.0f) return { 1.0f, 1.0f };           // true bypass
        p1_ = frac(p1_ + inc1_); p2_ = frac(p2_ + inc2_); p3_ = frac(p3_ + inc3_);
        const float ampLfo   = 0.6f * lfoSin(p1_) + 0.4f * lfoSin(p2_);   // [-1,1]
        const float pitchLfo = lfoSin(p3_);
        float amp = 1.0f + depth_ * kAmpDepth * ampLfo;
        if (amp < 0.0f) amp = 0.0f;
        const float pitch = 1.0f + depth_ * kPitchDepth * pitchLfo;
        return { amp, pitch };
    }

private:
    void updateIncs() {
        const float base = baseHz_ * rateJit_ / (float)fs_;
        inc1_ = base; inc2_ = base * 1.314f; inc3_ = base * 0.71f;
    }
    static float frac(float x) { x -= (float)(int)x; return (x < 0.0f) ? x + 1.0f : x; }
    static float lfoSin(float p) {                           // p in [0,1) -> ~sin(2*pi*p)
        const float x  = (p < 0.5f) ? 2.0f * p : 2.0f * p - 2.0f;   // [-1,1)
        const float ax = (x < 0.0f) ? -x : x;
        return 4.0f * x * (1.0f - ax);                       // parabolic sine
    }

    static constexpr int   kSeeds      = 6;       // matches FreezeEngine::kMaxLayers
    static constexpr float kAmpDepth   = 0.6f;    // max +/-60% amplitude swing
    static constexpr float kPitchDepth = 0.006f;  // max +/-0.6% (~+/-10 cents)

    double fs_ = 48000.0;
    float  baseHz_ = 0.5f, rateJit_ = 1.0f, depth_ = 0.0f;
    float  p1_ = 0.0f, p2_ = 0.0f, p3_ = 0.0f;
    float  inc1_ = 0.0f, inc2_ = 0.0f, inc3_ = 0.0f;
};

} // namespace boreas
