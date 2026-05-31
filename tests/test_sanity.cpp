#include "test_util.hpp"
int main() {
    CHECK(approx(1.0, 1.0));
    CHECK(1 + 1 == 2);
    REPORT();
}
