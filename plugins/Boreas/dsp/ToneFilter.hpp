#pragma once
#include <cmath>

namespace boreas {

// One-pole high-cut ("Tone"/"Sustain") filter. tone in [0,1]: 1 = open
// (~18 kHz, essentially no filtering), 0 = dark (~500 Hz). Emulates the natural
// high-frequency roll-off of a decaying note and tames any harshness.
class ToneFilter {
public:
    void prepare(double fs) { fs_ = fs; setTone(1.0); reset(); }
    void reset()            { y_ = 0.0f; }

    void setTone(double t) {
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        double fc = std::exp(std::log(500.0) * (1.0 - t) + std::log(18000.0) * t);
        const double nyq = fs_ * 0.45;
        if (fc > nyq) fc = nyq;
        a_ = (float)std::exp(-2.0 * M_PI * fc / fs_);
    }

    float process(float x) { y_ = (1.0f - a_) * x + a_ * y_; return y_; }

private:
    double fs_ = 48000.0;
    float  a_ = 0.0f, y_ = 0.0f;
};

} // namespace boreas
