/*
 * MyPlugin — minimal DPF plugin example for MOD Desktop / MOD Dwarf.
 *
 * Mono input → stereo output, with a single GAIN knob (±24 dB). Replace
 * the run() body with your own DSP, add parameters as needed.
 *
 * Useful patterns to crib (see INSTRUCTIONS.md):
 *   - Equal-power dry/wet crossfade for a MIX knob
 *   - Per-resonator tanh saturator inside feedback loops
 *   - One-pole envelope follower for noise gates
 *   - Parameter smoothing to avoid zipper noise (NOT done here yet —
 *     trivial DSP doesn't need it, but anything you build on top should)
 */

#include "DistrhoPlugin.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class MyPluginPlugin : public Plugin
{
public:
    enum ParamIndex {
        kParamGain = 0,
        kNumParams
    };

    MyPluginPlugin()
        : Plugin(kNumParams, 0, 0)
    {
        fGainDb = 0.0f;
    }

protected:
    // ---------------- Information ----------------
    // Derive label / maker / homepage from DISTRHO_PLUGIN_INFO macros so
    // the stable and beta builds report distinct identity to the host.

    const char* getLabel()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getMaker()       const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage()    const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    const char* getDescription() const override
    {
        return "A minimal mono-to-stereo plugin template.";
    }

    int64_t getUniqueId() const override
    {
        // Must match the 4-char DISTRHO_PLUGIN_UNIQUE_ID from
        // DistrhoPluginInfo.h. d_cconst packs four chars into an int32_t.
#ifdef MYPLUGIN_BETA
        return d_cconst('d', 'M', 'y', 'B');
#else
        return d_cconst('d', 'M', 'y', 'P');
#endif
    }

    // ---------------- Init ----------------

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        if (input)
            port.groupId = kPortGroupMono;
        else
            port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index)
        {
        case kParamGain:
            // dB-scale gain trim. Range ±24 covers typical input/output
            // matching needs. Pick a unit string so the host displays
            // values readably (MOD-UI uses this in tooltips).
            parameter.hints      = kParameterIsAutomatable;
            parameter.name       = "Gain";
            parameter.symbol     = "gain";
            parameter.unit       = "dB";
            parameter.ranges.min = -24.0f;
            parameter.ranges.max =  24.0f;
            parameter.ranges.def =   0.0f;
            break;
        }
    }

    // ---------------- State access ----------------

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamGain: return fGainDb;
        }
        return 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamGain:
            if (value < -24.0f) value = -24.0f;
            if (value >  24.0f) value =  24.0f;
            fGainDb = value;
            break;
        }
    }

    // ---------------- Lifecycle ----------------

    void activate() override
    {
        // Initialise any DSP state here (clear buffers, reset filters).
    }

    void sampleRateChanged(double newSampleRate) override
    {
        // Recompute sample-rate-dependent coefficients here when fs
        // changes (e.g. filter cutoffs, envelope time constants).
        (void) newSampleRate;
    }

    // ---------------- DSP ----------------

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const float* const in   = inputs[0];
        /* */ float* const outL = outputs[0];
        /* */ float* const outR = outputs[1];

        // dB → linear amplitude, once per block. For zipper-free knob
        // turns on more complex plugins, smooth the gain across the
        // block (linear ramp from old to new) — overkill for this trim.
        const float gainLin = std::pow(10.0f, fGainDb * (1.0f / 20.0f));

        for (uint32_t f = 0; f < frames; ++f)
        {
            const float y = in[f] * gainLin;
            outL[f] = y;
            outR[f] = y;
        }
    }

private:
    float fGainDb = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyPluginPlugin)
};

// ---------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new MyPluginPlugin();
}

END_NAMESPACE_DISTRHO
