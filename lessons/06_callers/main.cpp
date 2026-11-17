// 06 课示例：同一个热点函数，被两条长度不同的调用链调用。
// 用 perf 的调用栈回答"热点是被谁调用的"。
// 用法：./06_callers [倍率]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace lesson06 {

constexpr std::size_t kElems = 32ULL * 1024 * 1024;  // 256 MiB

std::vector<std::uint64_t> g_table;
std::uint64_t g_scale = 1;

// 热点：在 256 MiB 的表里追指针，每次访问都要等内存。
__attribute__((noinline)) std::uint64_t mix(std::uint64_t seed, std::uint64_t rounds) {
  std::uint64_t idx = seed & (kElems - 1);
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < rounds; ++i) {
    idx = g_table[idx];
    sum += idx;
  }
  return sum;
}

// 链一，三层：handle -> decode -> mix
// 拿到结果后再做一次运算，避免编译器把调用优化成尾调用、抹掉栈帧。
__attribute__((noinline)) std::uint64_t decode(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 6'000'000 * g_scale);
  return r ^ seed;
}

__attribute__((noinline)) std::uint64_t handle(std::uint64_t seed) {
  const std::uint64_t r = decode(seed + 1);
  return r + seed;
}

// 链二，两层：render -> mix
__attribute__((noinline)) std::uint64_t render(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 4'000'000 * g_scale);
  return r ^ seed;
}

}  // namespace lesson06

int main(int argc, char** argv) {
  if (argc > 1) {
    lesson06::g_scale = std::strtoull(argv[1], nullptr, 10);
  }
  if (lesson06::g_scale == 0) {
    lesson06::g_scale = 1;
  }

  lesson06::g_table.resize(lesson06::kElems);
  const std::uint64_t mask = lesson06::kElems - 1;
  for (std::size_t i = 0; i < lesson06::kElems; ++i) {
    lesson06::g_table[i] = (i * 1103515245ULL + 12345ULL) & mask;
  }

  // 种子取自运行时变量，避免编译器按常量把整条调用链特化成副本
  const std::uint64_t seed = 0x1234 ^ lesson06::g_scale;
  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t a = lesson06::handle(seed);
  const std::uint64_t b = lesson06::render(seed + 0x4444);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "handle = " << a << "\n"
            << "render = " << b << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
