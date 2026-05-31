#pragma once
#include <cmath>

namespace boreas {

constexpr double kMaxSampleRate = 96000.0;
constexpr double kRingSeconds   = 1.0;
constexpr double kWindowSec     = 0.15;
constexpr double kSpeedMinSec   = 0.005;   // at speed = 1.0 (fast)
constexpr double kSpeedMaxSec   = 4.0;     // at speed = 0.0 (slow)
constexpr double kGlissMaxSec   = 2.0;     // at gliss = 1.0

// Capture offset behind the write head, in samples. Default 0 = "freeze now".
// Kept as a single constant so it can later be promoted to an LV2 port for
// A/B testing transient-avoidance lookback without a DSP rewrite.
constexpr int kDefaultLookbackSamples = 0;

// Allocation ceilings (sized once; run() never allocates).
constexpr int kMaxRingSamples   = (int)(kRingSeconds * kMaxSampleRate) + 1;
constexpr int kMaxWindowSamples = (int)(kWindowSec   * kMaxSampleRate) + 2;

// Grain window length for a sample rate, rounded DOWN to even so W/2 is exact.
inline int windowSamples(double sampleRate) {
    int w = (int)(kWindowSec * sampleRate);
    if (w < 2) w = 2;
    if (w & 1) --w;
    return w;
}

// Speed knob (0..1) -> seconds; exponential, slow (kSpeedMaxSec) at 0, fast at 1.
inline double speedToSeconds(double speed) {
    if (speed < 0.0) speed = 0.0;
    if (speed > 1.0) speed = 1.0;
    return kSpeedMaxSec * std::pow(kSpeedMinSec / kSpeedMaxSec, speed);
}

// Gliss knob (0..1) -> morph length in samples; squared for finer low end.
inline int glissSamples(double gliss, double sampleRate) {
    if (gliss < 0.0) gliss = 0.0;
    if (gliss > 1.0) gliss = 1.0;
    return (int)(gliss * gliss * kGlissMaxSec * sampleRate);
}

} // namespace boreas
