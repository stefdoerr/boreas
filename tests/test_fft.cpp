#include "test_util.hpp"
#include "FFT.hpp"
#include <vector>
#include <cmath>
using namespace boreas;

static void test_roundtrip() {
    const int n = 64;
    std::vector<float> re(n), im(n, 0.0f), re0(n);
    for (int i = 0; i < n; ++i) {
        re[i] = (float)(std::sin(2*M_PI*3*i/n) + 0.5*std::cos(2*M_PI*7*i/n));
        re0[i] = re[i];
    }
    fft(re.data(), im.data(), n, false);
    fft(re.data(), im.data(), n, true);
    for (int i = 0; i < n; ++i) { re[i] /= n; im[i] /= n; }
    for (int i = 0; i < n; ++i) CHECK(approx(re[i], re0[i], 1e-4));
}
static void test_cosine_peak() {
    const int n = 64;
    std::vector<float> re(n), im(n, 0.0f);
    for (int i = 0; i < n; ++i) re[i] = (float)std::cos(2*M_PI*5*i/n);
    fft(re.data(), im.data(), n, false);
    int best = 0; double bm = 0;
    for (int k = 1; k < n/2; ++k) { double m = std::hypot(re[k], im[k]); if (m > bm) { bm = m; best = k; } }
    CHECK(best == 5);
}
static void test_impulse_flat() {
    const int n = 32;
    std::vector<float> re(n, 0.0f), im(n, 0.0f); re[0] = 1.0f;
    fft(re.data(), im.data(), n, false);
    for (int k = 0; k < n; ++k) CHECK(approx(std::hypot(re[k], im[k]), 1.0, 1e-4));
}
int main() { test_roundtrip(); test_cosine_peak(); test_impulse_flat(); REPORT(); }
