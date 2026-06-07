#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "FFT.hpp"

namespace boreas {

// Sinusoidal-model freeze voice. analyze() takes one windowed frame, FFTs it,
// extracts spectral peaks (parabolic-interpolated frequency/amplitude/phase),
// and stores them as oscillator-bank parameters. process() then sums a bank of
// steady sinusoids — no loop, no seam, so the sustain is dead steady and holds
// the full spectrum (chords included). FFT runs only at analyze (capture) time;
// the per-sample cost is the oscillator bank (sine LUT).
class SinusoidalModel {
public:
    // windowLen: the analysis window length this voice will be fed (the engine's
    // capture size). Precomputing the Hann window here keeps the ~7200-pt std::cos
    // loop OFF the audio thread — building it lazily in analyzeBegin() spiked the
    // first freeze into each layer slot by ~70 us (a guaranteed xrun on the Dwarf).
    void prepare(double fs, int windowLen = 0) {
        fs_ = fs;
        re_.assign(kFFT, 0.0f);
        im_.assign(kFFT, 0.0f);
        lut_.assign(kLUT + 1, 0.0f);
        for (int i = 0; i <= kLUT; ++i) lut_[i] = (float)std::sin(2.0 * M_PI * i / kLUT);
        twRe_.assign(kFFT / 2, 0.0f);                 // FFT twiddles, precomputed off the RT path
        twIm_.assign(kFFT / 2, 0.0f);
        p2_.assign(kFFT / 2, 0.0f);                    // power-spectrum scratch (peak picking)
        for (int j = 0; j < kFFT / 2; ++j) {
            const double a = 2.0 * M_PI * j / (double)kFFT;
            twRe_[j] = (float)std::cos(a);
            twIm_[j] = (float)std::sin(a);
        }
        const int wl = (windowLen > kFFT) ? kFFT : windowLen;
        if (wl > 0) buildHann(wl);                     // precompute so analyzeBegin never builds at RT
        nPeaks_ = 0;
    }

    void clear() { nPeaks_ = nPlay_ = 0; }
    // CPU budget: cap how many partials actually run as oscillators. Only ever
    // REDUCES (the stored peaks are amplitude-ordered, so this drops the quietest
    // ones); the engine shares one budget across stacked layers so total CPU is
    // bounded no matter how many layers are frozen.
    void setMaxOscillators(int n) {
        if (n < 0) n = 0; else if (n > nPeaks_) n = nPeaks_;
        if (n < nPlay_) nPlay_ = n;
    }
    int   peakCount() const { return nPeaks_; }
    int   liveOscillators() const { return nPlay_; }   // partials actually summed per sample
    float peakFreq(int i) const { return freq_[i]; }
    float peakAmp(int i)  const { return amp_[i]; }

    // Analysis is INCREMENTAL so the heavy 8192-pt FFT can be spread across many
    // audio blocks (a few stages per block) instead of spiking one callback and
    // xrunning on a slow CPU. analyzeBegin() does only the cheap windowing at
    // capture time; then call analyzeStep() once or a few times per block until it
    // returns true (peaks ready). analyze() runs the whole thing in one go (tests).
    void analyzeBegin(const float* buf, int n, int maxPeaks) {
        const int N = kFFT;
        aM_   = (n < N) ? n : N;              // window length (<= FFT size)
        aOff_ = n - aM_;                      // most-recent aM_ samples
        aMaxPeaks_ = maxPeaks;
        if ((int)hann_.size() != aM_) buildHann(aM_);   // normally precomputed in prepare(); rebuild only on size change
        double sumsq = 0.0;
        for (int i = 0; i < N; ++i) { re_[i] = 0.0f; im_[i] = 0.0f; }
        for (int i = 0; i < aM_; ++i) {
            const float s = buf[aOff_ + i];
            sumsq += (double)s * s;
            re_[i] = s * hann_[i];
        }
        aInputRMS_ = (float)std::sqrt(sumsq / (double)aM_);
        nPeaks_ = nPlay_ = 0;                 // silent until analysis completes
        aState_ = AS_BITREV;
    }

    // Advance the analysis by one chunk; returns true when complete.
    bool analyzeStep() {
        const int N = kFFT;
        switch (aState_) {
        case AS_BITREV:
            for (int i = 1, j = 0; i < N; ++i) {
                int bit = N >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) { std::swap(re_[i], re_[j]); std::swap(im_[i], im_[j]); }
            }
            aLen_ = 2; aState_ = AS_FFT;
            return false;
        case AS_FFT: {                       // one radix-2 stage (precomputed forward twiddles)
            const int len = aLen_, half = len >> 1, stride = N / len;
            for (int k = 0; k < half; ++k) {
                const float wr = twRe_[k * stride], wi = -twIm_[k * stride];
                for (int i = k; i < N; i += len) {
                    const int a = i, b = i + half;
                    const float vr = re_[b] * wr - im_[b] * wi;
                    const float vi = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - vr; im_[b] = im_[a] - vi;
                    re_[a] = re_[a] + vr; im_[a] = im_[a] + vi;
                }
            }
            aLen_ <<= 1;
            if (aLen_ > N) aState_ = AS_POWER;
            return false;
        }
        case AS_POWER: {                     // magnitude-squared spectrum (no per-bin sqrt)
            const int H = N / 2;
            float pmax = 1e-24f;
            for (int k = 1; k < H; ++k) { const float p = re_[k]*re_[k] + im_[k]*im_[k]; p2_[k] = p; if (p > pmax) pmax = p; }
            aThr_ = pmax * (kThreshRel * kThreshRel);
            aState_ = AS_PEAKS;
            return false;
        }
        case AS_PEAKS: {                     // pick peaks, parabolic-interpolate freq/amp/phase
            const int H = N / 2;
            nt_ = 0;
            for (int k = 2; k < H - 1 && nt_ < kCollect; ++k) {
                const float pL = p2_[k-1], pC = p2_[k], pR = p2_[k+1];
                if (pC > pL && pC >= pR && pC > aThr_) {
                    const float a = std::log(pL + 1e-24f), b = std::log(pC + 1e-24f), c = std::log(pR + 1e-24f);
                    const float den = a - 2*b + c;
                    float d = (std::fabs(den) > 1e-12f) ? 0.5f * (a - c) / den : 0.0f;
                    if (d > 0.5f) d = 0.5f; else if (d < -0.5f) d = -0.5f;
                    tmp_[nt_].f = (k + d) * (float)fs_ / N;
                    tmp_[nt_].a = std::exp(0.5f * (b - 0.25f * (a - c) * d));   // 0.5: log-power -> magnitude
                    tmp_[nt_].p = std::atan2(im_[k], re_[k]);
                    ++nt_;
                }
            }
            aState_ = AS_FINALIZE;
            return false;
        }
        case AS_FINALIZE: {                  // sort + min-separation select + write oscillator bank
            std::sort(tmp_, tmp_ + nt_, [](const Peak& x, const Peak& y) { return x.a > y.a; });
            const int cap = (aMaxPeaks_ < kMaxPeaks) ? aMaxPeaks_ : kMaxPeaks;
            // Greedy minimum-separation (strongest first): keep one peak per
            // ~kMinPeakSepHz band so smeared-cluster partials can't BEAT each other.
            Peak sel[kMaxPeaks]; int keep = 0;
            for (int i = 0; i < nt_ && keep < cap; ++i) {
                bool ok = true;
                for (int j = 0; j < keep; ++j)
                    if (std::fabs(tmp_[i].f - sel[j].f) < kMinPeakSepHz) { ok = false; break; }
                if (ok) sel[keep++] = tmp_[i];
            }
            double sm = 0.0;
            for (int i = 0; i < keep; ++i) sm += (double)sel[i].a * sel[i].a;
            const float scale = (sm > 1e-12) ? aInputRMS_ / (float)std::sqrt(0.5 * sm) : 0.0f;
            for (int i = 0; i < keep; ++i) {
                freq_[i]   = sel[i].f;
                amp_[i]    = sel[i].a * scale;             // fold level into amplitude
                // Fixed-point oscillator setup: a full 2^32 phase span = one cycle,
                // so the accumulator wraps on overflow (no branch) and the LUT index
                // is a bit-shift (no float<->int round-trip). See process().
                const double incd = (double)sel[i].f / fs_ * 4294967296.0;  // phase units / sample
                phinc_[i]  = (uint32_t)(incd + 0.5);
                phincF_[i] = (float)incd;
                double t = (double)sel[i].p * (1.0 / (2.0 * M_PI));         // initial phase -> turns
                t -= std::floor(t);                                        // wrap to [0,1)
                phacc_[i]  = (uint32_t)(t * 4294967296.0);
            }
            nPeaks_ = keep; nPlay_ = keep;             // play all by default; engine may cap via setMaxOscillators
            aState_ = AS_DONE;
            return true;
        }
        default:
            return true;
        }
    }

    // Run the whole analysis at once (non-RT use + tests).
    void analyze(const float* buf, int n, int maxPeaks) {
        analyzeBegin(buf, n, maxPeaks);
        while (!analyzeStep()) {}
    }

    // One output sample: sum the oscillator bank. Each partial is a fixed-point
    // phase accumulator (uint32, full span = one cycle): it wraps on overflow and
    // indexes the sine LUT with a bit-shift — no per-oscillator float<->int convert
    // or wrap branch, which is what made the old float-phase loop slow on the
    // Dwarf's in-order core. pitchScale (Movement shimmer / GLISS) detunes every
    // partial; pitchScale == 1.0 is the common path and runs as pure integer steps.
    float process(float pitchScale = 1.0f) {
        float y = 0.0f;
        const float fracScale = 1.0f / (float)(1u << kPhFrac);   // low bits -> frac [0,1)
        if (pitchScale == 1.0f) {
            for (int i = 0; i < nPlay_; ++i) {     // nPlay_ <= nPeaks_ (shared oscillator budget)
                const uint32_t p = phacc_[i];
                const uint32_t idx = p >> kPhFrac;
                const float frac = (float)(p & kPhFracMask) * fracScale;
                y += amp_[i] * (lut_[idx] + (lut_[idx + 1] - lut_[idx]) * frac);
                phacc_[i] = p + phinc_[i];
            }
        } else {
            for (int i = 0; i < nPlay_; ++i) {
                const uint32_t p = phacc_[i];
                const uint32_t idx = p >> kPhFrac;
                const float frac = (float)(p & kPhFracMask) * fracScale;
                y += amp_[i] * (lut_[idx] + (lut_[idx + 1] - lut_[idx]) * frac);
                phacc_[i] = p + (uint32_t)(int64_t)(phincF_[i] * pitchScale);
            }
        }
        return y;
    }

private:
    void buildHann(int m) {
        hann_.assign((size_t)m, 0.0f);
        for (int i = 0; i < m; ++i) hann_[i] = 0.5f - 0.5f * (float)std::cos(2.0 * M_PI * i / (double)m);
    }

    static constexpr int   kFFT      = 8192;
    static constexpr int   kMaxPeaks = 128;
    static constexpr int   kCollect  = 256;
    static constexpr int   kLUT      = 4096;          // power of two
    static constexpr int   kLutBits  = 12;            // kLUT == 1<<kLutBits
    static constexpr int   kPhFrac   = 32 - kLutBits; // phase fractional bits in the uint32 accumulator
    static constexpr uint32_t kPhFracMask = (1u << kPhFrac) - 1u;
    static_assert(kLUT == (1 << kLutBits), "kLutBits must match kLUT");
    static constexpr float kThreshRel = 0.001f;       // -60 dB relative peak threshold
    static constexpr float kMinPeakSepHz = 60.0f;     // min spacing between kept partials (anti-beating)

    double fs_ = 48000.0;
    std::vector<float> re_, im_, lut_, twRe_, twIm_, hann_, p2_;
    int   nPeaks_ = 0, nPlay_ = 0;
    float    freq_[kMaxPeaks], amp_[kMaxPeaks], phincF_[kMaxPeaks];   // freq (Hz), amplitude, float phase increment
    uint32_t phacc_[kMaxPeaks], phinc_[kMaxPeaks];                    // fixed-point phase accumulator + integer increment

    // Incremental-analysis state (see analyzeBegin / analyzeStep).
    struct Peak { float f, a, p; };
    enum  AState { AS_DONE = 0, AS_BITREV, AS_FFT, AS_POWER, AS_PEAKS, AS_FINALIZE };
    Peak   tmp_[kCollect];
    int    nt_ = 0, aM_ = 0, aOff_ = 0, aMaxPeaks_ = 0, aLen_ = 2;
    float  aInputRMS_ = 0.0f, aThr_ = 0.0f;
    AState aState_ = AS_DONE;
};

} // namespace boreas
