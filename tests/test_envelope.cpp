#include "test_util.hpp"
#include "EnvelopeGenerator.hpp"
using namespace boreas;

static void test_attack_reaches_one() {
    EnvelopeGenerator e; e.setSampleRate(1000.0); e.setTimes(0.1, 0.2);  // 100-sample attack
    e.attack();
    for (int i = 0; i < 100; ++i) e.process();
    CHECK(approx(e.value(), 1.0, 1e-3));
    CHECK(e.state() == EnvelopeGenerator::Sustain);
}
static void test_release_reaches_zero() {
    EnvelopeGenerator e; e.setSampleRate(1000.0); e.setTimes(0.1, 0.2);
    e.attack();  for (int i = 0; i < 100; ++i) e.process();
    e.release(); for (int i = 0; i < 200; ++i) e.process();
    CHECK(approx(e.value(), 0.0, 1e-3));
    CHECK(e.state() == EnvelopeGenerator::Idle);
    CHECK(!e.isActive());
}
static void test_monotonic_attack() {
    EnvelopeGenerator e; e.setSampleRate(1000.0); e.setTimes(0.05, 0.05);
    e.attack(); float prev = e.value();
    for (int i = 0; i < 10; ++i) { float v = e.process(); CHECK(v >= prev - 1e-6f); prev = v; }
}
int main() {
    test_attack_reaches_one();
    test_release_reaches_zero();
    test_monotonic_attack();
    REPORT();
}
