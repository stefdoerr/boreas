#pragma once
#include <cstdio>
#include <cmath>

inline int& failures() { static int f = 0; return f; }

#define CHECK(cond) do { if (!(cond)) { failures()++; \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

inline bool approx(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps;
}

#define REPORT() do { \
    if (failures()) { std::printf("FAILED (%d failed checks)\n", failures()); return 1; } \
    std::printf("OK\n"); return 0; } while (0)
