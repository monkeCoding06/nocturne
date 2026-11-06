#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Minimal test harness: no external dependency.
namespace testing {

struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Registrar {
  Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

}  // namespace testing

#define TEST(name)                                                  \
  static void test_##name();                                        \
  static testing::Registrar reg_##name(#name, test_##name);         \
  static void test_##name()

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      ++testing::failures();                                                          \
    }                                                                                 \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
  do {                                                                                                 \
    const double va_ = static_cast<double>(a), vb_ = static_cast<double>(b);                           \
    if (!(std::fabs(va_ - vb_) <= (tol))) {                                                            \
      std::printf("    FAIL %s:%d: %s ~ %s  (%g vs %g, tol %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_, \
                  static_cast<double>(tol));                                                           \
      ++testing::failures();                                                                           \
    }                                                                                                  \
  } while (0)
