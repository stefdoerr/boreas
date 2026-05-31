#pragma once
#include <cmath>
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
    void prepare(double fs) {
        fs_ = fs;
        re_.assign(kFFT, 0.0f);
        im_.assign(kFFT, 0.0f);
        lut_.assign(kLUT + 1, 0.0f);
        for (int i = 0; i <= kLUT; ++i) lut_[i] = (float)std::sin(2.0 * M_PI * i / kLUT);
        lutScale_ = (float)(kLUT / (2.0 * M_PI));
        nPeaks_ = 0;
    }

    void clear() { nPeaks_ = 0; }
    int   peakCount() const { return nPeaks_; }
    float peakFreq(int i) const { return freq_[i]; }
    float peakAmp(int i)  const { return amp_[i]; }

    // Extract up to `maxPeaks` partials from the most-recent samples of buf[0..n).
    void analyze(const float* buf, int n, int maxPeaks) {
        const int N = kFFT;
        const int M = (n < N) ? n : N;        // window length (≤ FFT size)
        const int off = n - M;                // most-recent M samples

        double sumsq = 0.0;
        for (int i = 0; i < N; ++i) { re_[i] = 0.0f; im_[i] = 0.0f; }
        for (int i = 0; i < M; ++i) {
            const float s = buf[off + i];
            sumsq += (double)s * s;
            const float w = 0.5f - 0.5f * (float)std::cos(2.0 * M_PI * i / (double)M);  // Hann
            re_[i] = s * w;
        }
        const float inputRMS = (float)std::sqrt(sumsq / (double)M);

        fft(re_.data(), im_.data(), N, false);
        const int H = N / 2;

        float mmax = 1e-12f;
        for (int k = 1; k < H; ++k) { const float m = mag(k); if (m > mmax) mmax = m; }
        const float thr = mmax * kThreshRel;

        struct Pk { float f, a, p; };
        Pk tmp[kCollect]; int nt = 0;
        for (int k = 2; k < H - 1 && nt < kCollect; ++k) {
            const float m0 = mag(k-1), m1 = mag(k), m2 = mag(k+1);
            if (m1 > m0 && m1 >= m2 && m1 > thr) {
                const float a = std::log(m0 + 1e-12f), b = std::log(m1 + 1e-12f), c = std::log(m2 + 1e-12f);
                const float den = a - 2*b + c;
                float d = (std::fabs(den) > 1e-12f) ? 0.5f * (a - c) / den : 0.0f;
                if (d > 0.5f) d = 0.5f; else if (d < -0.5f) d = -0.5f;
                tmp[nt].f = (k + d) * (float)fs_ / N;
                tmp[nt].a = std::exp(b - 0.25f * (a - c) * d);
                tmp[nt].p = std::atan2(im_[k], re_[k]);
                ++nt;
            }
        }
        std::sort(tmp, tmp + nt, [](const Pk& x, const Pk& y) { return x.a > y.a; });
        const int cap = (maxPeaks < kMaxPeaks) ? maxPeaks : kMaxPeaks;

        // Greedy minimum-separation selection (strongest first): keep one peak
        // per ~kMinPeakSepHz band and drop the close weaker ones. A captured frame
        // of real audio smears each partial into a cluster of neighbouring peaks;
        // summed as steady sinusoids those clusters BEAT against each other, an
        // amplitude flutter in the choppy 15-60 Hz range. Enforcing separation
        // pushes any residual beating above ~kMinPeakSepHz, where it reads as
        // timbre rather than chop.
        Pk sel[kMaxPeaks]; int keep = 0;
        for (int i = 0; i < nt && keep < cap; ++i) {
            bool ok = true;
            for (int j = 0; j < keep; ++j)
                if (std::fabs(tmp[i].f - sel[j].f) < kMinPeakSepHz) { ok = false; break; }
            if (ok) sel[keep++] = tmp[i];
        }

        double sm = 0.0;
        for (int i = 0; i < keep; ++i) sm += (double)sel[i].a * sel[i].a;
        const float scale = (sm > 1e-12) ? inputRMS / (float)std::sqrt(0.5 * sm) : 0.0f;

        const float w2pi = 2.0f * (float)M_PI / (float)fs_;
        nPeaks_ = keep;
        for (int i = 0; i < keep; ++i) {
            freq_[i]   = sel[i].f;
            amp_[i]    = sel[i].a * scale;                 // fold level into amplitude
            phase_[i]  = sel[i].p;
            dphase_[i] = sel[i].f * w2pi;
            osc_[i]    = (sel[i].p < 0.0f) ? sel[i].p + 2.0f*(float)M_PI : sel[i].p;
        }
    }

    // One output sample: sum the oscillator bank.
    float process() {
        float y = 0.0f;
        const float twoPi = 2.0f * (float)M_PI;
        for (int i = 0; i < nPeaks_; ++i) {
            y += amp_[i] * fastSin(osc_[i]);
            osc_[i] += dphase_[i];
            if (osc_[i] >= twoPi) osc_[i] -= twoPi;
        }
        return y;
    }

    void resetPhases() {
        for (int i = 0; i < nPeaks_; ++i)
            osc_[i] = (phase_[i] < 0.0f) ? phase_[i] + 2.0f*(float)M_PI : phase_[i];
    }

    // Layering: merge another model's partials in, scaling our own by selfGain,
    // keeping the strongest kMaxPeaks.
    void merge(const SinusoidalModel& o, float selfGain) {
        struct Pk { float f, a, p, d, osc; };
        Pk tmp[kMaxPeaks * 2]; int nt = 0;
        for (int i = 0; i < nPeaks_   && nt < kMaxPeaks*2; ++i) tmp[nt++] = {freq_[i],   amp_[i]*selfGain, phase_[i],   dphase_[i],   osc_[i]};
        for (int i = 0; i < o.nPeaks_ && nt < kMaxPeaks*2; ++i) tmp[nt++] = {o.freq_[i], o.amp_[i],        o.phase_[i], o.dphase_[i], o.osc_[i]};
        std::sort(tmp, tmp + nt, [](const Pk& x, const Pk& y) { return x.a > y.a; });
        const int keep = (nt > kMaxPeaks) ? kMaxPeaks : nt;
        nPeaks_ = keep;
        for (int i = 0; i < keep; ++i) {
            freq_[i] = tmp[i].f; amp_[i] = tmp[i].a; phase_[i] = tmp[i].p;
            dphase_[i] = tmp[i].d; osc_[i] = tmp[i].osc;
        }
    }

private:
    float mag(int k) const { return std::sqrt(re_[k]*re_[k] + im_[k]*im_[k]); }
    float fastSin(float ph) const {
        const float x = ph * lutScale_;
        int i = (int)x; const float frac = x - (float)i;
        i &= (kLUT - 1);
        return lut_[i] + (lut_[i+1] - lut_[i]) * frac;
    }

    static constexpr int   kFFT      = 8192;
    static constexpr int   kMaxPeaks = 128;
    static constexpr int   kCollect  = 256;
    static constexpr int   kLUT      = 4096;          // power of two
    static constexpr float kThreshRel = 0.001f;       // -60 dB relative peak threshold
    static constexpr float kMinPeakSepHz = 60.0f;     // min spacing between kept partials (anti-beating)

    double fs_ = 48000.0;
    std::vector<float> re_, im_, lut_;
    float lutScale_ = 1.0f;
    int   nPeaks_ = 0;
    float freq_[kMaxPeaks], amp_[kMaxPeaks], phase_[kMaxPeaks], dphase_[kMaxPeaks], osc_[kMaxPeaks];
};

} // namespace boreas
