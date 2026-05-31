#include "test_util.hpp"
#include "Constants.hpp"
using namespace boreas;

static void test_window_even() {
    CHECK(windowSamples(48000.0) == 7200);        // 0.15*48000
    CHECK(windowSamples(44100.0) == 6614);        // 6615 -> rounded down to even
    CHECK((windowSamples(44100.0) % 2) == 0);
}
static void test_speed_mapping() {
    CHECK(approx(speedToSeconds(0.0), kSpeedMaxSec, 1e-6));   // slow at 0
    CHECK(approx(speedToSeconds(1.0), kSpeedMinSec, 1e-6));   // fast at 1
    CHECK(speedToSeconds(0.4) > speedToSeconds(0.6));         // monotonic decreasing
}
static void test_gliss_mapping() {
    CHECK(glissSamples(0.0, 48000.0) == 0);                  // off
    CHECK(glissSamples(1.0, 1000.0) == 2000);                // 1*1*2.0*1000
}
int main() {
    test_window_even();
    test_speed_mapping();
    test_gliss_mapping();
    REPORT();
}
