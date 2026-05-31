/*
 * Boreas — freeze / sound-retainer pedal (LV2 / MOD).
 * Spec: docs/superpowers/specs/2026-05-30-boreas-freeze-design.md
 */
#include "DistrhoPlugin.hpp"
#include <cmath>
#include <cstdint>

#include "dsp/FreezeEngine.hpp"

// Flush-to-zero / denormals-are-zero: denormalised floats (from decaying filter
// and oscillator state) are ~100x slower on x86 and cause CPU spikes -> xruns
// at small buffer sizes. Enable per-audio-thread at the top of run().
#if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
  #include <pmmintrin.h>
  static inline void boreasFlushDenormals() {
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  }
#elif defined(__aarch64__)
  static inline void boreasFlushDenormals() {
      uint64_t fpcr; __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
      __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr | (1ULL << 24)));  // FZ bit
  }
#else
  static inline void boreasFlushDenormals() {}
#endif

START_NAMESPACE_DISTRHO

class BoreasPlugin : public Plugin
{
public:
    enum ParamIndex {
        kParamFootswitch = 0,
        kParamClear,
        kParamMode,
        kParamSpeed,
        kParamLayer,
        kParamGliss,
        kParamDryVol,
        kParamEffectVol,
        kParamMethod,
        kParamTone,
        kParamMoveRate,
        kParamMoveDepth,
        kNumParams
    };

    BoreasPlugin() : Plugin(kNumParams, 0, 0) {
        updateSmoothing(48000.0);
        dryS_ = fDryVol; effS_ = fEffectVol;
    }

protected:
    const char* getLabel()       const override { return DISTRHO_PLUGIN_NAME; }
    const char* getMaker()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage()    const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    const char* getDescription() const override {
        return "Freeze / sound retainer: captures a slice of audio and loops it into an infinite drone.";
    }
    int64_t getUniqueId() const override {
#ifdef BOREAS_BETA
        return d_cconst('d', 'B', 'o', 'B');
#else
        return d_cconst('d', 'B', 'o', 'r');
#endif
    }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override {
        port.groupId = kPortGroupMono;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override {
        switch (index) {
        case kParamFootswitch:
            parameter.hints  = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
            parameter.name   = "Freeze";  parameter.symbol = "footswitch";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamClear:
            parameter.hints  = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
            parameter.name   = "Clear";   parameter.symbol = "clear";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamMode: {
            parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
            parameter.name   = "Mode";    parameter.symbol = "mode";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 1.0f;
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const ev = new ParameterEnumerationValue[2];
            ev[0].value = 0.0f; ev[0].label = "Moment";
            ev[1].value = 1.0f; ev[1].label = "Latch";
            parameter.enumValues.values = ev;
            break;
        }
        case kParamSpeed:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Speed";   parameter.symbol = "speed";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.2f;
            break;
        case kParamLayer:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Layer";   parameter.symbol = "layer";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 1.0f;
            break;
        case kParamGliss:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Gliss";   parameter.symbol = "gliss";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamDryVol:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Dry";     parameter.symbol = "dry_vol";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.5f;
            break;
        case kParamEffectVol:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Effect";  parameter.symbol = "effect_vol";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.5f;
            break;
        case kParamMethod: {
            parameter.hints  = kParameterIsAutomatable | kParameterIsInteger;
            parameter.name   = "Method"; parameter.symbol = "method";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const ev = new ParameterEnumerationValue[2];
            ev[0].value = 0.0f; ev[0].label = "Sinusoidal";
            ev[1].value = 1.0f; ev[1].label = "Loop";
            parameter.enumValues.values = ev;
            break;
        }
        case kParamTone:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Tone"; parameter.symbol = "tone";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.4f;
            break;
        case kParamMoveRate:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Move Rate";  parameter.symbol = "move_rate";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.3f;
            break;
        case kParamMoveDepth:
            parameter.hints  = kParameterIsAutomatable;
            parameter.name   = "Move Depth"; parameter.symbol = "move_depth";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override {
        switch (index) {
        case kParamFootswitch: return fFootswitch;
        case kParamClear:      return fClear;
        case kParamMode:       return fMode;
        case kParamSpeed:      return fSpeed;
        case kParamLayer:      return fLayer;
        case kParamGliss:      return fGliss;
        case kParamDryVol:     return fDryVol;
        case kParamEffectVol:  return fEffectVol;
        case kParamMethod:     return fMethod;
        case kParamTone:       return fTone;
        case kParamMoveRate:   return fMoveRate;
        case kParamMoveDepth:  return fMoveDepth;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override {
        switch (index) {
        case kParamFootswitch: fFootswitch = value; break;
        case kParamClear:      fClear = value;      break;
        case kParamMode:       fMode = value;       break;
        case kParamSpeed:      fSpeed = value;      break;
        case kParamLayer:      fLayer = value;      break;
        case kParamGliss:      fGliss = value;      break;
        case kParamDryVol:     fDryVol = value;     break;
        case kParamEffectVol:  fEffectVol = value;  break;
        case kParamMethod:     fMethod = value;     break;
        case kParamTone:       fTone = value;       break;
        case kParamMoveRate:   fMoveRate = value;   break;
        case kParamMoveDepth:  fMoveDepth = value;  break;
        }
    }

    void activate() override {
        engine_.prepare(getSampleRate());
        updateSmoothing(getSampleRate());
        fsPrev_ = false; clrPrev_ = false; clrWiped_ = false; clrHeldFrames_ = 0;
        clrHoldThresh_ = (uint32_t)(0.4 * getSampleRate());   // hold >= 0.4s wipes all layers
        dryS_ = fDryVol; effS_ = fEffectVol;
    }

    void sampleRateChanged(double newSampleRate) override {
        engine_.prepare(newSampleRate);
        updateSmoothing(newSampleRate);
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        boreasFlushDenormals();

        const float* const in  = inputs[0];
        float* const       out = outputs[0];

        engine_.setMode((int)(fMode + 0.5f));
        engine_.setMethod((int)(fMethod + 0.5f));
        engine_.setTone(fTone);
        engine_.setSpeed(fSpeed);
        engine_.setLayer(fLayer);
        engine_.setGliss(fGliss);
        engine_.setMoveRate(fMoveRate);
        engine_.setMoveDepth(fMoveDepth);

        const bool fsNow = fFootswitch >= 0.5f;
        if (fsNow && !fsPrev_) engine_.onFreezePress();
        if (!fsNow && fsPrev_) engine_.onFreezeRelease();
        fsPrev_ = fsNow;

        // Clear: a tap (released before the hold threshold) removes the most
        // recent layer; a hold (>= threshold) wipes all layers.
        const bool clrNow = fClear >= 0.5f;
        if (clrNow && !clrPrev_) { clrHeldFrames_ = 0; clrWiped_ = false; }
        if (clrNow) {
            clrHeldFrames_ += frames;
            if (!clrWiped_ && clrHeldFrames_ >= clrHoldThresh_) { engine_.clearAllLayers(); clrWiped_ = true; }
        } else if (clrPrev_ && !clrWiped_) {
            engine_.removeLastLayer();
        }
        clrPrev_ = clrNow;

        for (uint32_t f = 0; f < frames; ++f) {
            engine_.writeInput(in[f]);
            const float wet = engine_.process();
            dryS_ += (fDryVol    - dryS_) * smoothCoef_;
            effS_ += (fEffectVol - effS_) * smoothCoef_;
            out[f] = dryS_ * in[f] + effS_ * wet;
        }
    }

private:
    void updateSmoothing(double fs) {
        smoothCoef_ = (float)(1.0 - std::exp(-1.0 / (fs * 0.005)));   // ~5 ms
    }

    float fFootswitch = 0.0f, fClear = 0.0f, fMode = 1.0f;
    float fSpeed = 0.2f, fLayer = 1.0f, fGliss = 0.0f;
    float fDryVol = 0.5f, fEffectVol = 0.5f;
    float fMethod = 0.0f;   // 0 = Sinusoidal, 1 = Loop
    float fTone = 0.4f;     // high-cut: 1 = open, 0 = dark (~2 kHz default tames frozen-noise buzz)
    float fMoveRate = 0.3f, fMoveDepth = 0.0f;   // Movement LFO (depth 0 = off, bit-identical)

    bool     fsPrev_ = false, clrPrev_ = false, clrWiped_ = false;
    uint32_t clrHeldFrames_ = 0, clrHoldThresh_ = 19200;   // ~0.4 s @ 48 kHz
    float dryS_ = 0.5f, effS_ = 0.5f, smoothCoef_ = 0.1f;

    boreas::FreezeEngine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoreasPlugin)
};

Plugin* createPlugin() { return new BoreasPlugin(); }

END_NAMESPACE_DISTRHO
