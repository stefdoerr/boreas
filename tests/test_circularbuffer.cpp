#include "test_util.hpp"
#include "CircularBuffer.hpp"
using namespace boreas;

static void test_recent_window() {
    CircularBuffer cb; cb.init(10);
    for (int i = 1; i <= 10; ++i) cb.write((float)i);   // holds 1..10, head wrapped to 0
    float dest[3];
    cb.copyWindow(dest, 3, 0);                          // 3 most-recent, chronological
    CHECK(approx(dest[0], 8.0));
    CHECK(approx(dest[1], 9.0));
    CHECK(approx(dest[2], 10.0));
}
static void test_lookback_offset() {
    CircularBuffer cb; cb.init(10);
    for (int i = 1; i <= 10; ++i) cb.write((float)i);
    float dest[3];
    cb.copyWindow(dest, 3, 2);                          // ending 2 behind head -> 6,7,8
    CHECK(approx(dest[0], 6.0));
    CHECK(approx(dest[2], 8.0));
}
static void test_wrap_continuous() {
    CircularBuffer cb; cb.init(4);
    for (int i = 1; i <= 6; ++i) cb.write((float)i);    // last 4: 3,4,5,6
    float dest[4];
    cb.copyWindow(dest, 4, 0);
    CHECK(approx(dest[0], 3.0));
    CHECK(approx(dest[3], 6.0));
}
static void test_clear() {
    CircularBuffer cb; cb.init(4);
    cb.write(5.0f); cb.clear();
    float dest[4]; cb.copyWindow(dest, 4, 0);
    CHECK(approx(dest[0], 0.0));
    CHECK(approx(dest[3], 0.0));
}
int main() {
    test_recent_window();
    test_lookback_offset();
    test_wrap_continuous();
    test_clear();
    REPORT();
}
