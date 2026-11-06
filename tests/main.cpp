#include <cstdio>
#include <cstring>

#include "TestHarness.hpp"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0, bad = 0;
  for (auto& c : testing::registry()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    const int before = testing::failures();
    c.fn();
    ++run;
    const bool ok = testing::failures() == before;
    if (!ok) ++bad;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", c.name);
  }
  std::printf("\n%d tests, %d failed\n", run, bad);
  return bad == 0 ? 0 : 1;
}
