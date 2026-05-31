#include "test_util.hpp"
#include "Modulator.hpp"
#include <cmath>
using namespace boreas;

// Depth = 0 must be a perfect bypass: every sample returns exactly {1, 1}.
static void test_depth_zero_is_unity() {
    Modulator m; m.prepare(48000.0); m.reset(0); m.setRate(0.5); m.setDepth(0.0);
    for (int i = 0; i < 20000; ++i) {
        Modulator::Out o = m.step();
        CHECK(o.amp == 1.0f); CHECK(o.pitch == 1.0f);
    }
}

// At full depth the amplitude breathes around 1.0 with a real swing, never < 0.
static void test_amp_breathes_centered() {
    Modulator m; m.prepare(48000.0); m.reset(0); m.setRate(0.8); m.setDepth(1.0);
    double sum = 0.0, lo = 1e9, hi = -1e9; const int N = 480000;   // 10 s
    for (int i = 0; i < N; ++i) { const float a = m.step().amp; sum += a; if (a < lo) lo = a; if (a > hi) hi = a; }
    CHECK(std::fabs(sum / N - 1.0) < 0.05);   // centred on unity
    CHECK(hi > 1.2 && lo < 0.8);              // meaningful swing
    CHECK(lo >= 0.0);                         // never negative
}

// Pitch shimmer is real but tiny (well under +/-1%).
static void test_pitch_shimmer_subtle() {
    Modulator m; m.prepare(48000.0); m.reset(0); m.setRate(0.8); m.setDepth(1.0);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 480000; ++i) { const float p = m.step().pitch; if (p < lo) lo = p; if (p > hi) hi = p; }
    CHECK(hi < 1.01f && lo > 0.99f);          // subtle
    CHECK(hi > 1.001f);                        // but it moves
}

// Two layers (different seeds) breathe independently.
static void test_layers_decorrelate() {
    Modulator a, b; a.prepare(48000.0); b.prepare(48000.0);
    a.reset(0); b.reset(3); a.setRate(0.8); b.setRate(0.8); a.setDepth(1.0); b.setDepth(1.0);
    double diff = 0.0; const int N = 240000;
    for (int i = 0; i < N; ++i) diff += std::fabs(a.step().amp - b.step().amp);
    CHECK(diff / N > 0.05);                    // independent movement
}

int main() {
    test_depth_zero_is_unity();
    test_amp_breathes_centered();
    test_pitch_shimmer_subtle();
    test_layers_decorrelate();
    REPORT();
}
