#pragma once
#include <cmath>
#include <utility>

namespace boreas {

// In-place iterative radix-2 Cooley-Tukey FFT. `n` MUST be a power of two.
// re/im are length n. inverse=false -> forward (no scaling); inverse=true ->
// inverse (also unscaled; divide by n yourself).
//
// Optionally pass a precomputed twiddle table (twRe[j]=cos(2*pi*j/n),
// twIm[j]=sin(2*pi*j/n) for j in [0, n/2)). Without it the twiddles are computed
// on the fly with cos/sin — fine for tests, but ~16k trig calls per 8192-pt
// transform, which is too slow for the audio thread on a slow CPU (causes an
// xrun/pop on freeze). At capture time we pass the table (see SinusoidalModel).
inline void fft(float* re, float* im, int n, bool inverse,
                const float* twRe = nullptr, const float* twIm = nullptr) {
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1;
        const int stride = n / len;
        const double base = (inverse ? 1.0 : -1.0) * 2.0 * M_PI / (double)len;
        for (int k = 0; k < half; ++k) {                 // twiddle per k, reused across groups
            float wr, wi;
            if (twRe) {                                  // precomputed table (forward sign)
                wr = twRe[k * stride];
                wi = inverse ? twIm[k * stride] : -twIm[k * stride];
            } else {
                const double ang = base * k;
                wr = (float)std::cos(ang); wi = (float)std::sin(ang);
            }
            for (int i = k; i < n; i += len) {
                const int a = i, b = i + half;
                const float vr = re[b] * wr - im[b] * wi;
                const float vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi;
                re[a] = re[a] + vr; im[a] = im[a] + vi;
            }
        }
    }
}

} // namespace boreas
