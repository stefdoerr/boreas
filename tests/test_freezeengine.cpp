#include "test_util.hpp"
#include "FreezeEngine.hpp"
#include <cmath>
#include <vector>
using namespace boreas;

static void writeSine(FreezeEngine& e, double f, double fs, int n) {
    for (int i = 0; i < n; ++i) e.writeInput(0.5f * (float)std::sin(2*M_PI*f*i/fs));
}
static void writeRich(FreezeEngine& e, double fs, int n) {   // ~60 harmonics -> many partials per layer
    for (int i = 0; i < n; ++i) {
        float x = 0.0f;
        for (int h = 1; h <= 60; ++h) x += (0.5f / h) * (float)std::sin(2*M_PI*(80.0*h)*i/fs);
        e.writeInput(0.2f * x);
    }
}
static void settle(FreezeEngine& e) { for (int i = 0; i < 120; ++i) e.tick(); }   // finish the chunked analysis

static void test_first_freeze_adds_layer() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0); e.setTone(1.0);
    writeSine(e, 220, 48000, 24000);
    CHECK(e.layerCount() == 0); CHECK(!e.active());
    e.onFreezePress();
    CHECK(e.layerCount() == 1); CHECK(e.active());
    settle(e);                                            // complete chunked analysis
    for (int i = 0; i < 500; ++i) e.process();            // past fade-in
    double sumsq = 0; for (int i = 0; i < 4096; ++i) { const float y = e.process(); sumsq += y*y; }
    CHECK(std::sqrt(sumsq/4096) > 0.05);                  // sustaining tone
}
static void test_stack_caps_at_max() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    for (int i = 0; i < 8; ++i) { e.onFreezePress(); for (int j = 0; j < 10; ++j) e.process(); }
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);    // capped at 6
}
static void test_osc_budget_bounds_cpu() {   // stacking must not let total oscillators grow without bound
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0); e.setTone(1.0);
    writeRich(e, 48000, 24000);                           // rich tone -> a single layer holds many partials
    e.onFreezePress(); settle(e);
    const int oneLayer = e.liveOscillators();
    CHECK(oneLayer >= 32);                                // one freeze plays a rich partial set
    for (int i = 0; i < 7; ++i) { e.onFreezePress(); settle(e); }   // stack past the max
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);
    const int sixLayers = e.liveOscillators();
    CHECK(sixLayers <= 96 + FreezeEngine::kMaxLayers);    // total bounded by the shared budget (+rounding)
    CHECK(sixLayers < oneLayer * 6);                      // budget actually clamped (uncapped would be ~6x)
}
static void test_remove_last_layer() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); e.onFreezePress(); e.onFreezePress();
    CHECK(e.layerCount() == 3);
    settle(e);
    e.removeLastLayer();
    CHECK(e.layerCount() == 2);
    for (int i = 0; i < 500; ++i) e.process();
    CHECK(e.active());                                    // two layers remain
}
static void test_clear_all() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); e.onFreezePress(); e.onFreezePress();
    settle(e);
    for (int i = 0; i < 500; ++i) e.process();            // ramp them in
    e.clearAllLayers();
    CHECK(e.layerCount() == 0);
    for (int i = 0; i < 2000; ++i) e.process();           // let fades complete
    CHECK(!e.active());
}
static void test_hold_press_release() {   // Hold footswitch: press = freeze, release = thaw
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onHoldPress();
    settle(e);
    for (int i = 0; i < 500; ++i) e.process();
    CHECK(e.layerCount() == 1); CHECK(e.active());
    e.onHoldRelease();
    CHECK(e.layerCount() == 0);
    for (int i = 0; i < 2000; ++i) e.process();
    CHECK(!e.active());                                   // faded out on release
}
static double envSpread(FreezeEngine& e, int blocks, int blk) {
    double lo = 1e9, hi = -1e9;
    for (int b = 0; b < blocks; ++b) {
        double s = 0.0; for (int i = 0; i < blk; ++i) { const float y = e.process(); s += (double)y*y; }
        const double r = std::sqrt(s / blk); if (r < lo) lo = r; if (r > hi) hi = r;
    }
    return hi - lo;
}
static void test_movement_breathes() {
    FreezeEngine a; a.prepare(48000.0);
    a.setSpeed(1.0); a.setGliss(0.0); a.setLayer(1.0); a.setTone(1.0);
    a.setMoveRate(0.8); a.setMoveDepth(0.0);                       // movement OFF
    writeSine(a, 220, 48000, 24000); a.onFreezePress(); settle(a); for (int i = 0; i < 1000; ++i) a.process();
    const double steady = envSpread(a, 200, 512);

    FreezeEngine b; b.prepare(48000.0);
    b.setSpeed(1.0); b.setGliss(0.0); b.setLayer(1.0); b.setTone(1.0);
    b.setMoveRate(0.8); b.setMoveDepth(1.0);                       // movement ON
    writeSine(b, 220, 48000, 24000); b.onFreezePress(); settle(b); for (int i = 0; i < 1000; ++i) b.process();
    const double breathing = envSpread(b, 200, 512);

    CHECK(breathing > steady * 3.0);                              // movement breathes; depth 0 stays steady
}
static double zcr(FreezeEngine& e, int n) {            // zero crossings over n samples
    int z = 0; float prev = 0.0f;
    for (int i = 0; i < n; ++i) { const float y = e.process(); if ((y >= 0.0f) != (prev >= 0.0f)) ++z; prev = y; }
    return (double)z;
}
static void test_gliss_slides_up_to_pitch() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setLayer(1.0);
    e.setTone(1.0); e.setMoveDepth(0.0); e.setGliss(1.0);              // full slide (~octave below, ~2s)
    writeSine(e, 300, 48000, 24000); e.onFreezePress(); settle(e);
    const double early = zcr(e, 4800);                                // start of glide: pitch low
    for (int i = 0; i < 130000; ++i) e.process();                     // skip past the 2s glide
    const double late = zcr(e, 4800);                                 // settled at native pitch
    CHECK(late > early * 1.4);                                        // pitch rose
}
int main() {
    test_first_freeze_adds_layer();
    test_stack_caps_at_max();
    test_osc_budget_bounds_cpu();
    test_remove_last_layer();
    test_clear_all();
    test_hold_press_release();
    test_movement_breathes();
    test_gliss_slides_up_to_pitch();
    REPORT();
}
