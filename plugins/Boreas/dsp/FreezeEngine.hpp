#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Constants.hpp"
#include "CircularBuffer.hpp"
#include "SinusoidalModel.hpp"
#include "ToneFilter.hpp"
#include "Modulator.hpp"

namespace boreas {

// Discrete layer-stack freeze (sinusoidal). Footswitch wiring is up to the host
// plugin: Freeze (trigger) calls onFreezePress per press to stack a layer; Hold
// (momentary) calls onFreezePress on press and onFreezeRelease on release to
// sustain a freeze only while held. Each Freeze press pushes a new frozen
// layer: a window is captured, FFT-analysed into a steady oscillator bank, and
// summed with the other layers. Clear removes the most-recent layer (or all).
// SPEED = per-layer volume fade in/out; GLISS = pitch slide-in (portamento);
// LAYER = new-layer level; Tone = output high-cut; Movement = per-layer amplitude
// breathing + pitch shimmer.
class FreezeEngine {
public:
    static constexpr int kMaxLayers = 6;

    void prepare(double sampleRate) {
        fs_ = sampleRate;
        W_  = windowSamples(sampleRate);
        ring_.init((int)(kRingSeconds * sampleRate) + 1);
        captureBuf_.assign((size_t)W_, 0.0f);
        for (int i = 0; i < kMaxLayers; ++i) {
            layers_[i].sin.prepare(sampleRate, W_);   // pass capture size -> Hann window precomputed off RT
            mod_[i].prepare(sampleRate);
        }
        tone_.prepare(sampleRate);
        reset();
    }

    void reset() {
        for (int i = 0; i < kMaxLayers; ++i) {
            layers_[i].sin.clear();
            layers_[i].active = layers_[i].fading = layers_[i].analyzing = false;
            layers_[i].gain = layers_[i].target = layers_[i].inc = 0.0f;
            layers_[i].glide = layers_[i].glideMul = 1.0f;
        }
        top_ = 0;
        tone_.reset();
    }

    void clearRing() { ring_.clear(); }

    // ---- per-block parameter push ----
    void setSpeed(double s) { speed_  = s; }
    void setLayer(double l) { layerLevel_ = (float)l; }   // level new layers are added at
    void setGliss(double g) { gliss_  = g; }
    void setTone(double t)  { tone_.setTone(t); }
    void setLookback(int n) { lookback_ = n; }
    void setMoveRate(double r)  { for (int i = 0; i < kMaxLayers; ++i) mod_[i].setRate(r); }
    void setMoveDepth(double d) { for (int i = 0; i < kMaxLayers; ++i) mod_[i].setDepth(d); }

    void writeInput(float x) { ring_.write(x); }

    // ---- footswitch events ----
    void onFreezePress()   { addLayer(); }          // Freeze: stack; Hold: begin sustain
    void onFreezeRelease() { removeLastLayer(); }    // Hold: end sustain (Freeze never calls this)

    void removeLastLayer() {                                    // Clear / Hold release
        if (top_ <= 0) return;
        Layer& L = layers_[top_ - 1];
        if (L.analyzing) { L.analyzing = false; L.active = false; }   // never sounded -> just drop it
        else beginFade(L, 0.0f, speedFadeSamples());
        --top_;
    }
    void clearAllLayers() {
        for (int i = 0; i < kMaxLayers; ++i) {
            if (!layers_[i].active) continue;
            if (layers_[i].analyzing) { layers_[i].analyzing = false; layers_[i].active = false; }
            else beginFade(layers_[i], 0.0f, speedFadeSamples());
        }
        top_ = 0;
    }

    // Advance in-progress freeze analysis off the capture block: a bounded number
    // of FFT steps per audio block, so the heavy 8192-pt transform never spikes one
    // callback (which xruns on a slow CPU). A new layer stays silent until its
    // analysis completes, then its volume fade-in begins. Call once per audio block.
    void tick() {
        int budget = kAnalyzeStepsPerTick;
        for (int i = 0; i < kMaxLayers && budget > 0; ++i) {
            Layer& L = layers_[i];
            if (!L.analyzing) continue;
            while (budget > 0) {
                --budget;
                if (L.sin.analyzeStep()) {                 // analysis finished -> start the fade-in
                    L.analyzing = false;
                    const int fade = speedFadeSamples();
                    L.inc = (fade > 0) ? L.target / (float)fade : L.target;
                    applyOscBudget();                      // cap the now-ready layer to its share
                    break;
                }
            }
        }
    }

    // ---- per-sample output ----
    float process() {
        float wet = 0.0f;
        for (int i = 0; i < kMaxLayers; ++i) {
            Layer& L = layers_[i];
            if (!L.active || L.analyzing) continue;            // analyzing -> silent until ready
            const Modulator::Out m = mod_[i].step();           // breathing + pitch shimmer
            const float gp = L.glide;                          // GLISS: slide up to pitch
            if (gp < 1.0f) { L.glide *= L.glideMul; if (L.glide > 1.0f) L.glide = 1.0f; }
            wet += L.sin.process(m.pitch * gp) * m.amp * L.gain;
            L.gain += L.inc;
            if (L.inc < 0.0f) { if (L.gain <= 0.0f)     { L.gain = 0.0f;     L.active = false; L.sin.clear(); } }
            else              { if (L.gain >= L.target) { L.gain = L.target; L.inc = 0.0f; } }
        }
        return std::tanh(tone_.process(wet));                   // tone + soft-limit when stacking
    }

    // ---- accessors (tests / GUI) ----
    int  windowLen()  const { return W_; }
    int  layerCount() const { return top_; }
    int  liveOscillators() const {                     // total partials summed per sample across all layers
        int n = 0;
        for (int i = 0; i < kMaxLayers; ++i) if (layers_[i].active) n += layers_[i].sin.liveOscillators();
        return n;
    }
    bool active() const {
        for (int i = 0; i < kMaxLayers; ++i) if (layers_[i].active) return true;
        return false;
    }

private:
    struct Layer {
        bool  active = false, fading = false, analyzing = false;
        float gain = 0.0f, target = 0.0f, inc = 0.0f;
        float glide = 1.0f, glideMul = 1.0f;   // GLISS pitch slide (ratio -> 1.0)
        SinusoidalModel sin;
    };

    int speedFadeSamples() const { int n = (int)(speedToSeconds(speed_) * fs_); return n < 64 ? 64 : n; }

    static void beginFade(Layer& L, float target, int samples) {
        L.fading = (target <= 0.0f);
        L.target = target;
        L.inc = (samples > 0) ? (target - L.gain) / (float)samples : (target - L.gain);
    }

    void addLayer() {
        if (top_ >= kMaxLayers) return;                         // stack full
        Layer& L = layers_[top_];
        ring_.copyWindow(captureBuf_.data(), W_, lookback_);
        L.sin.analyzeBegin(captureBuf_.data(), W_, kAnalyzePeaks);   // cheap windowing now; FFT spread over ticks
        L.analyzing = true;
        L.gain = 0.0f; L.inc = 0.0f; L.fading = false; L.active = true;
        L.target = layerLevel_;                                      // fade-in begins when analysis completes (tick)
        // GLISS: start this layer below pitch and slide up to it (bigger gliss =
        // deeper + slower swoop). gliss 0 -> no slide.
        if (gliss_ > 0.0) {
            const float start = std::pow(2.0f, -(float)gliss_); // gliss 1 -> 1 octave below
            int g = glissSamples(gliss_, fs_); if (g < 1) g = 1;
            L.glide = start;
            L.glideMul = std::pow(1.0f / start, 1.0f / (float)g);
        } else {
            L.glide = 1.0f; L.glideMul = 1.0f;
        }
        mod_[top_].reset((uint32_t)addSeed_);                   // fresh breathing per layer
        ++addSeed_; ++top_;
        applyOscBudget();                                       // shrink existing layers to share the budget
    }

    static constexpr int kAnalyzeStepsPerTick = 2;   // FFT chunks advanced per audio block
                                                     // (~17 steps total -> freeze sounds ~20 ms after press,
                                                     //  hidden by the fade-in; keeps each block's work tiny)
    // Oscillator cap: the per-sample oscillator-bank sum dominates Dwarf CPU, so
    // kOscBudget is a hard ceiling on TOTAL live partials across all active layers
    // (1 layer plays up to it; stacking divides it -> N layers ~ budget/N each), and
    // kAnalyzePeaks caps what a single freeze captures. History: the OLD float-phase
    // oscillator was glitch-free only up to 64. SinusoidalModel now uses a fixed-point
    // phase accumulator (~1.5x cheaper per partial on the Dwarf's in-order core), which
    // lifted the on-device ceiling to 96 (confirmed glitch-free at 6 layers; NCO@128
    // still xran at the 5th). To trial a richer value again, raise these behind
    // `#ifdef BOREAS_BETA` and A/B with `make BETA=1 dwarf` before promoting.
    static constexpr int kAnalyzePeaks = 96;
    static constexpr int kOscBudget    = 96;

    // Share the oscillator budget across the currently-active layers (called when
    // a layer is added or finishes analysing). Only ever reduces a layer's live
    // partial count, so it never re-expands (which would click).
    void applyOscBudget() {
        int n = 0;
        for (int i = 0; i < kMaxLayers; ++i) if (layers_[i].active) ++n;
        if (n < 1) return;
        const int cap = kOscBudget / n;
        for (int i = 0; i < kMaxLayers; ++i)
            if (layers_[i].active) layers_[i].sin.setMaxOscillators(cap);
    }

    double fs_ = 48000.0;
    int    W_ = 7200;
    int    lookback_ = kDefaultLookbackSamples;

    CircularBuffer     ring_;
    std::vector<float> captureBuf_;
    Layer              layers_[kMaxLayers];
    Modulator          mod_[kMaxLayers];
    ToneFilter         tone_;

    double speed_ = 0.2, gliss_ = 0.0;
    float  layerLevel_ = 1.0f;
    int    top_ = 0, addSeed_ = 0;
};

} // namespace boreas
