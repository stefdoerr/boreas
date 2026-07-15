// Layer-lifecycle regression tests: layer identity + explicit state machine.
// Covers the three bugs of the implicit top_/gain-sign bookkeeping: zombie
// layers at Layer=0, slot reuse hard-cutting a fade-out, and Hold release
// removing the wrong layer.
#include "test_util.hpp"
#include "FreezeEngine.hpp"
#include <cmath>
using namespace boreas;

static void writeSine(FreezeEngine& e, double f, double fs, int n) {
    for (int i = 0; i < n; ++i) e.writeInput(0.5f * (float)std::sin(2*M_PI*f*i/fs));
}
static void settle(FreezeEngine& e) { for (int i = 0; i < 120; ++i) e.tick(); }

// A freeze taken with the Layer knob at 0 must still be removable: Clear on a
// zero-gain layer used to leave it active forever (silent, but summing its
// full oscillator bank every sample and eating the shared budget).
static void test_layer_zero_freeze_is_clearable() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setTone(1.0);
    e.setLayer(0.0);                                      // new layers come in silent
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); settle(e);
    for (int i = 0; i < 48000; ++i) e.process();
    CHECK(e.layerCount() == 1);                           // in the stack, clearable
    e.removeLastLayer();
    CHECK(e.layerCount() == 0);
    for (int i = 0; i < 48000; ++i) e.process();          // let any fade finish
    CHECK(!e.active());                                   // fully freed, no zombie
    CHECK(e.liveOscillators() == 0);
}

// Freezing again while the cleared layer is still fading out must not cut the
// fading tail: the new layer should take a free slot, not overwrite the one
// that is still sounding.
static void test_refreeze_during_fadeout_keeps_tail() {
    FreezeEngine e; e.prepare(48000.0);
    e.setGliss(0.0); e.setTone(1.0); e.setLayer(1.0);
    e.setSpeed(1.0);                                      // fast fade-in for setup
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); settle(e);
    for (int i = 0; i < 48000; ++i) e.process();          // fully faded in
    e.setSpeed(0.0);                                      // ~4 s fade-out
    e.removeLastLayer();                                  // Clear -> slow fade begins
    for (int i = 0; i < 4800; ++i) e.process();           // 100 ms into the fade
    float before = 0.0f;
    for (int i = 0; i < 64; ++i) before = std::fmax(before, std::fabs(e.process()));
    e.onFreezePress();                                    // re-freeze mid-fade
    float after = 0.0f;
    for (int i = 0; i < 64; ++i) after = std::fmax(after, std::fabs(e.process()));
    CHECK(before > 0.1f);                                 // tail was audible
    CHECK(after > before * 0.5f);                         // and keeps ringing out
}

static double zcr(FreezeEngine& e, int n) {              // zero crossings over n samples
    int z = 0; float prev = 0.0f;
    for (int i = 0; i < n; ++i) { const float y = e.process(); if ((y >= 0.0f) != (prev >= 0.0f)) ++z; prev = y; }
    return (double)z;
}

// Releasing Hold must remove exactly the layer that Hold press created — not
// whatever layer happens to be newest. Hold a 220 Hz freeze, stack a 440 Hz
// Freeze layer on top, release Hold: the 440 Hz layer must survive.
static void test_hold_release_removes_hold_layer() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setTone(1.0); e.setLayer(1.0); e.setMoveDepth(0.0);
    writeSine(e, 220, 48000, 24000);
    e.onHoldPress();                                      // hold layer: 220 Hz
    settle(e); for (int i = 0; i < 4800; ++i) e.process();
    writeSine(e, 440, 48000, 24000);
    e.onFreezePress();                                    // freeze layer: 440 Hz
    settle(e); for (int i = 0; i < 4800; ++i) e.process();
    CHECK(e.layerCount() == 2);
    e.onHoldRelease();                                    // thaw the HOLD layer only
    CHECK(e.layerCount() == 1);
    for (int i = 0; i < 48000; ++i) e.process();          // hold layer fades out
    CHECK(e.active());                                    // freeze layer still sustains
    const double z = zcr(e, 4800);                        // 440 Hz -> ~88 crossings; 220 -> ~44
    CHECK(z > 66.0);
}

// Hold pressed on a full stack adds nothing, so its release must not delete
// someone else's freeze layer.
static void test_hold_release_on_full_stack_is_noop() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    for (int i = 0; i < FreezeEngine::kMaxLayers; ++i) { e.onFreezePress(); settle(e); }
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);
    e.onHoldPress();                                      // stack full -> no layer created
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);
    e.onHoldRelease();
    CHECK(e.layerCount() == FreezeEngine::kMaxLayers);    // nothing stolen
}

// Clear while holding removes the hold layer (it is the newest); the later
// Hold release must then be a clean no-op instead of removing another layer.
static void test_clear_while_holding_then_release_is_noop() {
    FreezeEngine e; e.prepare(48000.0);
    e.setSpeed(1.0); e.setGliss(0.0); e.setLayer(1.0);
    writeSine(e, 220, 48000, 24000);
    e.onFreezePress(); settle(e);                         // a normal freeze layer
    e.onHoldPress(); settle(e);                           // hold layer on top
    for (int i = 0; i < 4800; ++i) e.process();
    CHECK(e.layerCount() == 2);
    e.removeLastLayer();                                  // Clear removes the hold layer
    CHECK(e.layerCount() == 1);
    e.onHoldRelease();                                    // its layer is gone -> no-op
    CHECK(e.layerCount() == 1);                           // freeze layer untouched
    for (int i = 0; i < 48000; ++i) e.process();
    CHECK(e.active());                                    // and still sustaining
}

int main() {
    test_layer_zero_freeze_is_clearable();
    test_refreeze_during_fadeout_keeps_tail();
    test_hold_release_removes_hold_layer();
    test_hold_release_on_full_stack_is_noop();
    test_clear_while_holding_then_release_is_noop();
    REPORT();
}
