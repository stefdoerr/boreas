#include "test_util.hpp"
#include "SinusoidalModel.hpp"
#include <vector>
#include <cmath>
using namespace boreas;

static void test_analyze_finds_sine() {
    const int fs = 48000, n = 8192;
    std::vector<float> buf(n);
    for (int i = 0; i < n; ++i) buf[i] = 0.5f * (float)std::sin(2*M_PI*440*i/fs);
    SinusoidalModel m; m.prepare(fs);
    m.analyze(buf.data(), n, 16);
    CHECK(m.peakCount() >= 1);
    CHECK(std::fabs(m.peakFreq(0) - 440.0f) < 8.0f);    // dominant peak ≈ 440 Hz
}

static void test_resynth_level_and_frequency() {
    const int fs = 48000, n = 8192;
    std::vector<float> buf(n);
    for (int i = 0; i < n; ++i) buf[i] = 0.5f * (float)std::sin(2*M_PI*440*i/fs);
    SinusoidalModel m; m.prepare(fs);
    m.analyze(buf.data(), n, 16);
    const int N = 8192;
    std::vector<float> o(N);
    double sumsq = 0; int crossings = 0;
    for (int i = 0; i < N; ++i) {
        o[i] = m.process(); sumsq += o[i]*o[i];
        if (i > 0 && ((o[i-1] < 0) != (o[i] < 0))) ++crossings;
    }
    const double rms = std::sqrt(sumsq / N);
    CHECK(rms > 0.25 && rms < 0.45);                    // ≈ input RMS 0.354 (level matched)
    const double freq = (crossings / 2.0) / (N / (double)fs);
    CHECK(std::fabs(freq - 440.0) < 15.0);              // resynthesised tone ≈ 440 Hz
}

static void test_silence_no_peaks() {
    const int fs = 48000, n = 8192;
    std::vector<float> z(n, 0.0f);
    SinusoidalModel m; m.prepare(fs);
    m.analyze(z.data(), n, 16);
    CHECK(m.peakCount() == 0);
    CHECK(std::fabs(m.process()) < 1e-6f);
}

int main() {
    test_analyze_finds_sine();
    test_resynth_level_and_frequency();
    test_silence_no_peaks();
    REPORT();
}
