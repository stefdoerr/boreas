#include "test_util.hpp"
#include "FreezeEngine.hpp"
#include <cmath>
#include <vector>
using namespace boreas;

static void writeSine(FreezeEngine& e, double f, double fs, int n) {
    for (int i = 0; i < n; ++i) e.writeInput(0.5f * (float)std::sin(2*M_PI*f*i/fs));
}

static void test_first_freeze_adds_layer() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeLatch); e.setMethod(MethodSinusoidal); e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0); e.setTone(1.0);
    writeSine(e, 220, 48000, 24000);
    CHECK(e.layerCount() == 0); CHECK(!e.active());
    e.onFreezePress();
    CHECK(e.layerCount() == 1); CHECK(e.active());
    for (int i = 0; i < 500; ++i) e.process();            // past fade-in
    double sumsq = 0; for (int i = 0; i < 4096; ++i) { const float y = e.process(); sumsq += y*y; }
    CHECK(std::sqrt(sumsq/4096) > 0.05);                  // sustaining tone
}
static void test_stack_caps_at_max() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeLatch); e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    for (int i = 0; i < 8; ++i) { e.onFreezePress(); for (int j = 0; j < 10; ++j) e.process(); }
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);    // capped at 6
}
static void test_remove_last_layer() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeLatch); e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); e.onFreezePress(); e.onFreezePress();
    CHECK(e.layerCount() == 3);
    e.removeLastLayer();
    CHECK(e.layerCount() == 2);
    for (int i = 0; i < 500; ++i) e.process();
    CHECK(e.active());                                    // two layers remain
}
static void test_clear_all() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeLatch); e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); e.onFreezePress(); e.onFreezePress();
    for (int i = 0; i < 500; ++i) e.process();            // ramp them in
    e.clearAllLayers();
    CHECK(e.layerCount() == 0);
    for (int i = 0; i < 2000; ++i) e.process();           // let fades complete
    CHECK(!e.active());
}
static void test_moment_press_release() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeMoment); e.setSpeed(1.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress();
    for (int i = 0; i < 500; ++i) e.process();
    CHECK(e.layerCount() == 1); CHECK(e.active());
    e.onFreezeRelease();
    CHECK(e.layerCount() == 0);
    for (int i = 0; i < 2000; ++i) e.process();
    CHECK(!e.active());                                   // faded out on release
}
static void test_loop_method_sustains() {
    FreezeEngine e; e.prepare(48000.0);
    e.setMode(ModeLatch); e.setMethod(MethodLoop); e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0); e.setTone(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress();
    for (int i = 0; i < 500; ++i) e.process();
    double sumsq = 0; for (int i = 0; i < 4096; ++i) { const float y = e.process(); sumsq += y*y; }
    CHECK(std::sqrt(sumsq/4096) > 0.02);
}
int main() {
    test_first_freeze_adds_layer();
    test_stack_caps_at_max();
    test_remove_last_layer();
    test_clear_all();
    test_moment_press_release();
    test_loop_method_sustains();
    REPORT();
}
