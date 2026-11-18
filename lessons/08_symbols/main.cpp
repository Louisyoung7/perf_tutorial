// 08 课示例：一个热点循环，外加一个会被内联的小函数。
// 同一份源码编出三种二进制，用 perf 报告看符号和行号有什么区别。
// 用法：./08_symbols [倍率]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// 三个构建目标各定义一个值，运行时会打印出来，顺便让彼此的 build-id 不同
#ifndef LESSON08_VARIANT
#define LESSON08_VARIANT "full"
#endif

namespace lesson08 {

constexpr std::size_t kElems = 32ULL * 1024 * 1024;  // 256 MiB
std::vector<std::uint64_t> g_table;
std::uint64_t g_scale = 1;

// 小函数：-O2 会把它内联进 walk，符号表里找不到它
__attribute__((always_inline)) inline std::uint64_t next(std::uint64_t idx) {
  return g_table[idx];
}

// 热点：在 256 MiB 的表里追指针，每次读取都要等内存
__attribute__((noinline)) std::uint64_t walk(std::uint64_t seed, std::uint64_t rounds) {
  std::uint64_t idx = seed & (kElems - 1);
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < rounds; ++i) {
    idx = next(idx);
    sum += idx;
  }
  return sum;
}

}  // namespace lesson08

int main(int argc, char** argv) {
  if (argc > 1) {
    lesson08::g_scale = std::strtoull(argv[1], nullptr, 10);
  }
  if (lesson08::g_scale == 0) {
    lesson08::g_scale = 1;
  }

  lesson08::g_table.resize(lesson08::kElems);
  const std::uint64_t mask = lesson08::kElems - 1;
  for (std::size_t i = 0; i < lesson08::kElems; ++i) {
    lesson08::g_table[i] = (i * 1103515245ULL + 12345ULL) & mask;
  }

  const std::uint64_t seed = 0x1234 ^ lesson08::g_scale;
  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t a = lesson08::walk(seed, 10'000'000 * lesson08::g_scale);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "variant: " << LESSON08_VARIANT << "\n"
            << "walk = " << a << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
