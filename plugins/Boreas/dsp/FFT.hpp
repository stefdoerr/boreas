#pragma once
#include <cmath>
#include <utility>

namespace boreas {

// In-place iterative radix-2 Cooley-Tukey FFT. `n` MUST be a power of two.
// re/im are length n. inverse=false -> forward (no scaling); inverse=true ->
// inverse (also unscaled; divide by n yourself). Used only at capture time
// (one transform per freeze), so clarity/accuracy over micro-optimisation.
inline void fft(float* re, float* im, int n, bool inverse) {
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const double sign = inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1;
        const double base = sign * 2.0 * M_PI / (double)len;
        for (int k = 0; k < half; ++k) {                 // twiddle per k, reused across groups
            const double ang = base * k;
            const float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
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
