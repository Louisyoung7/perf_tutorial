// 03 课示例：两种访问方式、同样的迭代次数，
// 用 perf 采样看时间到底停在哪一行。
// 用法：./03_hot_line [迭代次数]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// 顺序扫描：地址连续，硬件可以预取，不等内存。
__attribute__((noinline)) std::uint64_t scan(const std::vector<std::uint64_t>& data,
                                             std::uint64_t n) {
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    sum += data[i];
  }
  return sum;
}

// 指针追逐：下一次读哪里由这次读到的值决定，只能串行等。
__attribute__((noinline)) std::uint64_t chase(const std::vector<std::uint64_t>& data,
                                              std::uint64_t n) {
  std::uint64_t idx = 0;
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    idx = data[idx];
    sum += idx;
  }
  return sum;
}

int main(int argc, char** argv) {
  std::uint64_t n = 10'000'000ULL;
  if (argc > 1) {
    n = std::strtoull(argv[1], nullptr, 10);
  }

  // 32M 个元素 = 256 MiB，远大于本机 36 MiB 的 L3
  std::vector<std::uint64_t> data(32ULL * 1024 * 1024);
  const std::uint64_t mask = data.size() - 1;
  for (std::size_t i = 0; i < data.size(); ++i) {
    // 把下标打乱成一条不重复的链，让指针追逐落到随机位置
    data[i] = (i * 1103515245ULL + 12345ULL) & mask;
  }

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t a = scan(data, n);
  const std::uint64_t b = chase(data, n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "scan(" << n << ") = " << a << "\n"
            << "chase(" << n << ") = " << b << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
