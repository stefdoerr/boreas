#include "test_util.hpp"
#include "ToneFilter.hpp"
#include <vector>
#include <cmath>
using namespace boreas;

static double rmsAt(float tone, double f, double fs) {
    ToneFilter t; t.prepare(fs); t.setTone(tone);
    const int n = (int)(fs * 0.2); double sumsq = 0; int cnt = 0;
    for (int i = 0; i < n; ++i) {
        const float y = t.process((float)std::sin(2*M_PI*f*i/fs));
        if (i > n/2) { sumsq += y*y; ++cnt; }
    }
    return std::sqrt(sumsq / cnt);
}
static void test_dc_passes() {
    ToneFilter t; t.prepare(48000); t.setTone(0.5);
    float y = 0; for (int i = 0; i < 4000; ++i) y = t.process(1.0f);
    CHECK(approx(y, 1.0, 0.02));                      // DC passes through
}
static void test_high_cut() {
    const double dark = rmsAt(0.1f, 8000, 48000);     // dark tone
    const double open = rmsAt(1.0f, 8000, 48000);     // open tone
    CHECK(dark < open);                               // darker attenuates 8 kHz more
    CHECK(open > 0.5);                                // open ~passes 8 kHz (in RMS)
    CHECK(dark < 0.3);                                // dark clearly cuts 8 kHz
}
int main() { test_dc_passes(); test_high_cut(); REPORT(); }
