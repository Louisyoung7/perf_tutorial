// 01 课示例：一个"一直在算"的程序，用来验证 perf 能采到样、能解析到符号和源码行。
// 用法：./01_environment [迭代次数]，默认 30 亿次，约 1 秒量级。

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// 故意不内联：让 perf report 里能看到独立的函数名
__attribute__((noinline)) std::uint64_t spin(std::uint64_t n) {
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    sum += i * i;
  }
  return sum;
}

int main(int argc, char** argv) {
  std::uint64_t n = 3'000'000'000ULL;
  if (argc > 1) {
    n = std::strtoull(argv[1], nullptr, 10);
  }

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t sum = spin(n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "spin(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
