#pragma once

#include <chrono>
#include <cstdio>

// Lightweight stage timing for performance work. Logs "[perf] <name> <ms>" to stderr.
inline void perfLog(const char *stage, double ms) {
  std::fprintf(stderr, "[perf] %-30s %9.2f ms\n", stage, ms);
}

class PerfScope {
public:
  explicit PerfScope(const char *name) : name_(name), start_(std::chrono::steady_clock::now()) {}
  ~PerfScope() {
    const auto end = std::chrono::steady_clock::now();
    perfLog(name_, std::chrono::duration<double, std::milli>(end - start_).count());
  }

private:
  const char *name_;
  std::chrono::steady_clock::time_point start_;
};
