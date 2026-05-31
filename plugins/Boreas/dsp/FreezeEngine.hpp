#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

#include "Constants.hpp"
#include "CircularBuffer.hpp"
#include "SinusoidalModel.hpp"
#include "ToneFilter.hpp"

namespace boreas {

enum Mode   { ModeMoment = 0, ModeLatch = 1 };
enum Method { MethodSinusoidal = 0, MethodLoop = 1 };

// Discrete layer-stack freeze. Each Freeze press pushes a new frozen layer
// (captured + analysed); layers play summed. Clear removes the most-recent
// layer (or all). Each layer fades in (GLISS for stacked layers, SPEED for the
// first / Moment) and fades out (SPEED). Method selects the per-layer voice:
// Sinusoidal (oscillator bank) or Loop (crossfade loop). Tone = output high-cut.
class FreezeEngine {
public:
    static constexpr int kMaxLayers = 6;

    void prepare(double sampleRate) {
        fs_ = sampleRate;
        W_  = windowSamples(sampleRate);
        C_  = W_ / 8; C_ &= ~1; if (C_ < 2) C_ = 2;
        ring_.init((int)(kRingSeconds * sampleRate) + 1);
        xfadeIn_.assign((size_t)C_, 0.0f);
        xfadeOut_.assign((size_t)C_, 0.0f);
        for (int k = 0; k < C_; ++k) {
            const double a = (k + 0.5) / (double)C_ * 1.5707963267948966;
            xfadeIn_[k]  = (float)std::sin(a);
            xfadeOut_[k] = (float)std::cos(a);
        }
        for (int i = 0; i < kMaxLayers; ++i) {
            layers_[i].buf.assign((size_t)W_, 0.0f);
            layers_[i].sin.prepare(sampleRate);
        }
        tone_.prepare(sampleRate);
        reset();
    }

    void reset() {
        for (int i = 0; i < kMaxLayers; ++i) {
            std::fill(layers_[i].buf.begin(), layers_[i].buf.end(), 0.0f);
            layers_[i].sin.clear();
            layers_[i].active = layers_[i].fading = false;
            layers_[i].gain = layers_[i].target = layers_[i].inc = 0.0f;
            layers_[i].phase = 0;
        }
        top_ = 0;
        tone_.reset();
    }

    void clearRing() { ring_.clear(); }

    // ---- per-block parameter push ----
    void setMode(int m)     { mode_   = m; }
    void setMethod(int m)   { method_ = m; }
    void setSpeed(double s) { speed_  = s; }
    void setLayer(double l) { layerLevel_ = (float)l; }   // level new layers are added at
    void setGliss(double g) { gliss_  = g; }
    void setTone(double t)  { tone_.setTone(t); }
    void setLookback(int n) { lookback_ = n; }

    void writeInput(float x) { ring_.write(x); }

    // ---- footswitch events ----
    void onFreezePress() {
        if (mode_ == ModeMoment) {
            if (top_ == 0) addLayer(speedFadeSamples());        // Moment: single layer
        } else {
            addLayer(top_ == 0 ? speedFadeSamples() : glissFadeSamples());  // Latch: stack
        }
    }
    void onFreezeRelease() { if (mode_ == ModeMoment) removeLastLayer(); }

    void removeLastLayer() {                                    // Clear tap
        if (top_ <= 0) return;
        beginFade(layers_[top_ - 1], 0.0f, speedFadeSamples());
        --top_;
    }
    void clearAllLayers() {                                     // Clear hold
        for (int i = 0; i < kMaxLayers; ++i)
            if (layers_[i].active) beginFade(layers_[i], 0.0f, speedFadeSamples());
        top_ = 0;
    }

    // ---- per-sample output ----
    float process() {
        float wet = 0.0f;
        for (int i = 0; i < kMaxLayers; ++i) {
            Layer& L = layers_[i];
            if (!L.active) continue;
            wet += voiceRead(L) * L.gain;
            L.gain += L.inc;
            if (L.inc < 0.0f) { if (L.gain <= 0.0f)     { L.gain = 0.0f;     L.active = false; L.sin.clear(); } }
            else              { if (L.gain >= L.target) { L.gain = L.target; L.inc = 0.0f; } }
        }
        return std::tanh(tone_.process(wet));                   // tone + soft-limit when stacking
    }

    // ---- accessors (tests / GUI) ----
    int  windowLen()  const { return W_; }
    int  layerCount() const { return top_; }
    bool active() const {
        for (int i = 0; i < kMaxLayers; ++i) if (layers_[i].active) return true;
        return false;
    }

private:
    struct Layer {
        bool  active = false, fading = false;
        float gain = 0.0f, target = 0.0f, inc = 0.0f;
        int   phase = 0;
        std::vector<float> buf;
        SinusoidalModel    sin;
    };

    int speedFadeSamples() const { int n = (int)(speedToSeconds(speed_) * fs_); return n < 64 ? 64 : n; }
    int glissFadeSamples() const { int n = glissSamples(gliss_, fs_);          return n < 240 ? 240 : n; }

    static void beginFade(Layer& L, float target, int samples) {
        L.fading = (target <= 0.0f);
        L.target = target;
        L.inc = (samples > 0) ? (target - L.gain) / (float)samples : (target - L.gain);
    }

    void addLayer(int fadeSamples) {
        if (top_ >= kMaxLayers) return;                         // stack full
        Layer& L = layers_[top_];
        ring_.copyWindow(L.buf.data(), W_, lookback_);
        L.sin.analyze(L.buf.data(), W_, kAnalyzePeaks);
        L.phase = 0; L.gain = 0.0f; L.fading = false;
        L.active = true;
        L.target = layerLevel_;
        L.inc = (fadeSamples > 0) ? L.target / (float)fadeSamples : L.target;
        ++top_;
    }

    float voiceRead(Layer& L) {
        return (method_ == MethodSinusoidal) ? L.sin.process() : readLoop(L.buf.data(), L.phase);
    }
    float readLoop(const float* buf, int& phase) {
        if (phase < 0 || phase >= W_) phase = C_;
        float y;
        if (phase < W_ - C_) y = buf[phase];
        else { const int t = phase - (W_ - C_); y = buf[phase] * xfadeOut_[t] + buf[t] * xfadeIn_[t]; }
        if (++phase >= W_) phase = C_;
        return y;
    }

    static constexpr int kAnalyzePeaks = 120;

    double fs_ = 48000.0;
    int    W_ = 7200, C_ = 900;
    int    lookback_ = kDefaultLookbackSamples;

    CircularBuffer     ring_;
    std::vector<float> xfadeIn_, xfadeOut_;
    Layer              layers_[kMaxLayers];
    ToneFilter         tone_;

    int    mode_ = ModeLatch, method_ = MethodSinusoidal;
    double speed_ = 0.2, gliss_ = 0.0;
    float  layerLevel_ = 1.0f;
    int    top_ = 0;
};

} // namespace boreas
