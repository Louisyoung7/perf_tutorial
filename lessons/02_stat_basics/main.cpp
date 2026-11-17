// 02 课示例：只有一个纯计算循环，没有数组、没有随机分支。
// 用法：./02_stat_basics [迭代次数]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// 循环体只有乘加，数据全在寄存器里。
// noinline 让函数边界留在报告里，方便对照。
__attribute__((noinline)) std::uint64_t run_arith(std::uint64_t n) {
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    sum += i * 3 + 1;
  }
  return sum;
}

int main(int argc, char** argv) {
  std::uint64_t n = 4'000'000'000ULL;
  if (argc > 1) {
    n = std::strtoull(argv[1], nullptr, 10);
  }

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t sum = run_arith(n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "arith(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
