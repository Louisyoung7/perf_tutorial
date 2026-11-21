// 09 课示例：一条等内存的加载指令，后面跟一小段廉价算术。
// 用来观察采样不准时，等待时间会被记到哪条指令上。
// 用法：./09_events [倍率]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace lesson09 {

constexpr std::size_t kElems = 8ULL * 1024 * 1024;  // 8M 个元素 = 64 MiB，大于测试环境 36 MiB 的 L3

std::vector<std::uint64_t> g_table;

// 热点：一次大概率未命中 L3 的读取
__attribute__((noinline)) std::uint64_t fetch(std::uint64_t idx) {
  return g_table[idx];
}

// 加载之后的一小段算术：本身很快，只是紧跟在慢加载后面
__attribute__((noinline)) std::uint64_t tail(std::uint64_t v) {
  v = v * 6364136223846793005ULL + 1442695040888963407ULL;
  v ^= v >> 33;
  v *= 0xff51afd7ed558ccdULL;
  v ^= v >> 33;
  v *= 0xc4ceb9fe1a85ec53ULL;
  v ^= v >> 33;
  return v;
}

__attribute__((noinline)) std::uint64_t wait_phase(std::uint64_t n) {
  std::uint64_t idx = 1;
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    idx = fetch(idx) & (kElems - 1);
    sum += tail(idx);
  }
  return sum;
}

}  // namespace lesson09

int main(int argc, char** argv) {
  std::uint64_t scale = 1;
  if (argc > 1) {
    scale = std::strtoull(argv[1], nullptr, 10);
  }

  lesson09::g_table.resize(lesson09::kElems);
  for (std::size_t i = 0; i < lesson09::g_table.size(); ++i) {
    // 把下标打乱成一条不重复的链，让每次读取都落到随机位置
    lesson09::g_table[i] = (i * 1103515245ULL + 12345ULL) & (lesson09::kElems - 1);
  }

  const std::uint64_t n = 10'000'000ULL * scale;

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t sum = lesson09::wait_phase(n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "wait_phase(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
