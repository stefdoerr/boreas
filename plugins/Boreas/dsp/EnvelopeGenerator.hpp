#pragma once

namespace boreas {

// Linear attack/release master envelope. Linear ramps are deterministic and
// reach their targets exactly (easy to reason about and test).
class EnvelopeGenerator {
public:
    enum State { Idle, Attack, Sustain, Release };

    void setSampleRate(double fs) { fs_ = fs; }

    void setTimes(double attackSec, double releaseSec) {
        attackInc_  = (attackSec  > 0.0) ? (1.0 / (attackSec  * fs_)) : 1.0;
        releaseDec_ = (releaseSec > 0.0) ? (1.0 / (releaseSec * fs_)) : 1.0;
    }

    void attack()  { state_ = Attack; }
    void release() { state_ = Release; }
    void reset()   { state_ = Idle; value_ = 0.0; }

    State state() const   { return state_; }
    float value() const   { return (float)value_; }
    bool  isActive() const { return state_ != Idle || value_ > 0.0; }

    float process() {
        if (state_ == Attack) {
            value_ += attackInc_;
            if (value_ >= 1.0) { value_ = 1.0; state_ = Sustain; }
        } else if (state_ == Release) {
            value_ -= releaseDec_;
            if (value_ <= 0.0) { value_ = 0.0; state_ = Idle; }
        }
        return (float)value_;
    }

private:
    double fs_ = 48000.0;
    double value_ = 0.0;
    double attackInc_ = 1.0;
    double releaseDec_ = 1.0;
    State  state_ = Idle;
};

} // namespace boreas
