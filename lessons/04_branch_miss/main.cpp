// 04 课示例：用随机位决定 if 走哪一边，让分支预测器猜不准。
// 用法：./04_branch_miss [迭代次数]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// 两个分支各调一个 noinline 函数。
// 如果两边都只写一条加减，编译器会改写成无分支指令，实验就做不成了。
__attribute__((noinline)) std::uint64_t on_odd(std::uint64_t sum, std::uint64_t x) {
  return sum + x;
}

__attribute__((noinline)) std::uint64_t on_even(std::uint64_t sum, std::uint64_t x) {
  return sum - x;
}

__attribute__((noinline)) std::uint64_t run_branch(std::uint64_t n) {
  std::uint64_t x = 0x9e3779b97f4a7c15ULL;
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    // xorshift：三次移位异或，只为了把最低位搅乱
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    if (x & 1) {
      sum = on_odd(sum, x);
    } else {
      sum = on_even(sum, x);
    }
  }
  return sum;
}

int main(int argc, char** argv) {
  std::uint64_t n = 300'000'000ULL;
  if (argc > 1) {
    n = std::strtoull(argv[1], nullptr, 10);
  }

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t sum = run_branch(n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "branch(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
