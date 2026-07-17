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
        kParamHold,
        kParamSpeed,
        kParamLayer,
        kParamGliss,
        kParamDryVol,
        kParamEffectVol,
        kParamTone,
        kParamMoveRate,
        kParamMoveDepth,
        // Deprecated ports, kept HIDDEN for backward-compatibility: a pedalboard
        // saved with an older Boreas references these symbols, and mod-ui KeyErrors
        // if a saved port symbol is no longer declared. The DSP ignores them.
        kParamMode,      // replaced by the Hold footswitch
        kParamMethod,    // removed when Boreas went Sinusoidal-only
        kNumParams
    };

    BoreasPlugin() : Plugin(kNumParams, 0, 0) {
        updateSmoothing(48000.0);
        dryS_ = fDryVol; effS_ = fEffectVol;
    }

protected:
    const char* getLabel()       const override { return DISTRHO_PLUGIN_NAME; }
    const char* getMaker()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage()    const override { return "https://github.com/stefdoerr/boreas"; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    const char* getDescription() const override {
        return "Freeze / infinite sustain. Captures a moment of sound and resynthesises it as a "
               "smooth, endless drone via a spectral oscillator bank. Stack up to six layers, "
               "slide them in by pitch (gliss), shape the high end (tone), and add organic "
               "movement (breathing + shimmer). Mono in/out.";
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
            // Trigger (pprops:trigger): MOD addresses a hardware footswitch as a
            // momentary pulse (press = on for one block, auto-off) instead of a
            // latching toggle, so every press fires a freeze. (kParameterIsTrigger
            // includes kParameterIsBoolean.)
            parameter.hints  = kParameterIsAutomatable | kParameterIsTrigger | kParameterIsInteger;
            parameter.name   = "Freeze";  parameter.symbol = "footswitch";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamClear:
            // Trigger, exactly like Freeze: each press removes the most-recent layer.
            // (A momentary Clear made the on-screen button sticky and the tap-vs-hold
            // timing fiddly; a clean pulse per press is what's wanted.)
            parameter.hints  = kParameterIsAutomatable | kParameterIsTrigger | kParameterIsInteger;
            parameter.name   = "Clear";   parameter.symbol = "clear";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamHold:
            // Momentary "hold to freeze" (mod:preferMomentaryOnByDefault, TTL-patched):
            // value is 1 while the footswitch is held; the freeze sustains only while
            // held and thaws on release.
            parameter.hints  = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
            parameter.name   = "Hold";    parameter.symbol = "hold";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
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
        // Deprecated, hidden, ignored — see the enum note (pedalboard backward-compat).
        case kParamMode:
            parameter.hints  = kParameterIsAutomatable | kParameterIsInteger | kParameterIsHidden;
            parameter.name   = "Mode (deprecated)";   parameter.symbol = "mode";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 1.0f;
            break;
        case kParamMethod:
            parameter.hints  = kParameterIsAutomatable | kParameterIsInteger | kParameterIsHidden;
            parameter.name   = "Method (deprecated)"; parameter.symbol = "method";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override {
        switch (index) {
        case kParamFootswitch: return fFootswitch;
        case kParamClear:      return fClear;
        case kParamHold:       return fHold;
        case kParamSpeed:      return fSpeed;
        case kParamLayer:      return fLayer;
        case kParamGliss:      return fGliss;
        case kParamDryVol:     return fDryVol;
        case kParamEffectVol:  return fEffectVol;
        case kParamTone:       return fTone;
        case kParamMoveRate:   return fMoveRate;
        case kParamMoveDepth:  return fMoveDepth;
        case kParamMode:       return fModeDeprecated_;
        case kParamMethod:     return fMethodDeprecated_;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override {
        switch (index) {
        case kParamFootswitch: fFootswitch = value; break;
        case kParamClear:      fClear = value;      break;
        case kParamHold:       fHold = value;       break;
        case kParamSpeed:      fSpeed = value;      break;
        case kParamLayer:      fLayer = value;      break;
        case kParamGliss:      fGliss = value;      break;
        case kParamDryVol:     fDryVol = value;     break;
        case kParamEffectVol:  fEffectVol = value;  break;
        case kParamTone:       fTone = value;       break;
        case kParamMoveRate:   fMoveRate = value;   break;
        case kParamMoveDepth:  fMoveDepth = value;  break;
        case kParamMode:       fModeDeprecated_ = value;   break;   // ignored
        case kParamMethod:     fMethodDeprecated_ = value; break;   // ignored
        }
    }

    void activate() override {
        engine_.prepare(getSampleRate());
        updateSmoothing(getSampleRate());
        fsPrev_ = false; hdPrev_ = false; clrPrev_ = false;
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

        engine_.setTone(fTone);
        engine_.setSpeed(fSpeed);
        engine_.setLayer(fLayer);
        engine_.setGliss(fGliss);
        engine_.setMoveRate(fMoveRate);
        engine_.setMoveDepth(fMoveDepth);

        // Freeze (trigger): each press stacks a layer. Ignore the release edge —
        // the trigger's auto-off must not remove the layer just added.
        const bool fsNow = fFootswitch >= 0.5f;
        if (fsNow && !fsPrev_) engine_.onFreezePress();
        fsPrev_ = fsNow;

        // Hold (momentary): freeze sustains only while held; the release removes
        // exactly the layer the press created (no-op if it was already cleared).
        const bool hdNow = fHold >= 0.5f;
        if (hdNow && !hdPrev_) engine_.onHoldPress();
        if (!hdNow && hdPrev_) engine_.onHoldRelease();
        hdPrev_ = hdNow;

        // Clear (trigger): each press removes the most-recent layer.
        const bool clrNow = fClear >= 0.5f;
        if (clrNow && !clrPrev_) engine_.removeLastLayer();
        clrPrev_ = clrNow;

        engine_.tick();   // advance any in-progress freeze analysis (spread off this block)

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

    float fFootswitch = 0.0f, fClear = 0.0f, fHold = 0.0f;
    float fSpeed = 0.2f, fLayer = 1.0f, fGliss = 0.0f;
    float fDryVol = 0.5f, fEffectVol = 0.5f;
    float fTone = 0.4f;     // high-cut: 1 = open, 0 = dark (~2 kHz default tames frozen-noise buzz)
    float fMoveRate = 0.3f, fMoveDepth = 0.0f;   // Movement LFO (depth 0 = off, bit-identical)
    float fModeDeprecated_ = 1.0f, fMethodDeprecated_ = 0.0f;   // hidden, ignored (backward-compat)

    bool     fsPrev_ = false, hdPrev_ = false, clrPrev_ = false;
    float dryS_ = 0.5f, effS_ = 0.5f, smoothCoef_ = 0.1f;

    boreas::FreezeEngine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoreasPlugin)
};

Plugin* createPlugin() { return new BoreasPlugin(); }

END_NAMESPACE_DISTRHO
