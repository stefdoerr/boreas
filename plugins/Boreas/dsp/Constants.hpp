#pragma once
#include <cmath>

namespace boreas {

constexpr double kMaxSampleRate = 96000.0;
// Input ring length. Only the most-recent kWindowSec is used today; the extra
// headroom is intentional reserve for a future Lookback control.
// TODO(lookback): expose lookback as an LV2 port; the ring + copyWindow already
// support it (see kDefaultLookbackSamples). Until then ~850 ms here is unused.
constexpr double kRingSeconds   = 1.0;
constexpr double kWindowSec     = 0.15;
constexpr double kSpeedMinSec   = 0.005;   // at speed = 1.0 (fast)
constexpr double kSpeedMaxSec   = 4.0;     // at speed = 0.0 (slow)
constexpr double kGlissMaxSec   = 2.0;     // at gliss = 1.0
constexpr double kMoveRateMinHz = 0.05;    // Movement LFO at rate = 0.0 (slow breath)
constexpr double kMoveRateMaxHz = 8.0;     // at rate = 1.0 (fast tremolo)

// Capture offset behind the write head, in samples. Default 0 = "freeze now".
// TODO(lookback): promote to an LV2 port so the user can freeze a window from
// *before* the stomp (dodge the attack transient / rewind). FreezeEngine already
// has setLookback() and CircularBuffer::copyWindow() already takes the offset —
// it just needs a parameter wired up. Currently always 0.
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

// Movement-rate knob (0..1) -> LFO frequency in Hz; exponential.
inline double moveRateToHz(double r) {
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    return kMoveRateMinHz * std::pow(kMoveRateMaxHz / kMoveRateMinHz, r);
}

} // namespace boreas
