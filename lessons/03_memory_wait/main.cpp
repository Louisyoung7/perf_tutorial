// 03 课示例：在一个远大于末级缓存的大数组里做指针追逐，
// 每次读到的值决定下一次读哪里，让 CPU 只能一等再等。
// 用法：./03_memory_wait [迭代次数]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

__attribute__((noinline)) std::uint64_t chase(const std::vector<std::uint64_t>& data,
                                              std::uint64_t n) {
  std::uint64_t idx = 0;
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    idx = data[idx];  // 下一次读哪里，取决于这次读到了什么
    sum += idx;
  }
  return sum;
}

int main(int argc, char** argv) {
  std::uint64_t n = 10'000'000ULL;
  if (argc > 1) {
    n = std::strtoull(argv[1], nullptr, 10);
  }

  // 32M 个元素 = 256 MiB，远大于测试环境 36 MiB 的 L3
  std::vector<std::uint64_t> data(32ULL * 1024 * 1024);
  const std::uint64_t mask = data.size() - 1;
  for (std::size_t i = 0; i < data.size(); ++i) {
    // 打乱成一条不重复的链，让每次访问都跳到随机位置
    data[i] = (i * 1103515245ULL + 12345ULL) & mask;
  }

  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t sum = chase(data, n);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "chase(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
