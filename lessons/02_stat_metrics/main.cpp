// 02 课示例：三个小内核，分别把 perf stat 里的三类指标"做"出来。
// 用法：./02_stat_metrics [arith|cache|branch] [迭代次数]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// 计算型：只有整数运算，没有分支，也不访问大块内存。
// 预期：IPC 高，cache-misses、branch-misses 都很低。
__attribute__((noinline)) std::uint64_t run_arith(std::uint64_t n) {
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    sum += i * 3 + 1;
  }
  return sum;
}

// 访存型：在一个远大于末级缓存的大数组里做伪随机"指针追逐"。
// 每一步的地址来自上一步读到的值，无法提前预取。
// 预期：cache-misses 占比高，IPC 低。
__attribute__((noinline)) std::uint64_t run_cache(const std::vector<std::uint64_t>& data,
                                                  std::uint64_t n) {
  std::uint64_t idx = 0;
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    idx = data[idx];  // 下一次访问的位置由这次读到的值决定
    sum += idx;
  }
  return sum;
}

// 分支型：用伪随机数决定走 if 的哪一边，分支结果无法预测。
// 两个分支各自调用一个 noinline 函数，防止编译器把 if 改写成无分支指令。
// 预期：branch-misses 占 branches 的比例高，IPC 居中。
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
  const char* mode = (argc > 1) ? argv[1] : "arith";

  std::uint64_t n = 0;
  if (std::strcmp(mode, "arith") == 0) {
    n = 4'000'000'000ULL;
  } else if (std::strcmp(mode, "branch") == 0) {
    n = 300'000'000ULL;
  } else if (std::strcmp(mode, "cache") == 0) {
    n = 10'000'000ULL;
  } else {
    std::cerr << "mode 必须是 arith / cache / branch\n";
    return 1;
  }
  if (argc > 2) {
    n = std::strtoull(argv[2], nullptr, 10);
  }

  // cache 模式用 256 MiB 的数组，比本机 36 MiB 的 L3 大得多
  std::vector<std::uint64_t> data;
  if (std::strcmp(mode, "cache") == 0) {
    data.resize(32ULL * 1024 * 1024);  // 32M 个元素 = 256 MiB
    const std::uint64_t mask = data.size() - 1;
    for (std::size_t i = 0; i < data.size(); ++i) {
      // 一个遍历全部下标的置换，形成一条很长的"随机"链表
      data[i] = (i * 1103515245ULL + 12345ULL) & mask;
    }
  }

  const auto begin = std::chrono::steady_clock::now();
  std::uint64_t sum = 0;
  if (std::strcmp(mode, "arith") == 0) {
    sum = run_arith(n);
  } else if (std::strcmp(mode, "cache") == 0) {
    sum = run_cache(data, n);
  } else {
    sum = run_branch(n);
  }
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << mode << "(" << n << ") = " << sum << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
