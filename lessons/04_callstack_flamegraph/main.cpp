// 04 课示例：同一个热点函数被三条不同的调用链调用，
// 用 perf 的调用栈看清热点是被谁调用的、链有多深。
// 用法：./04_callstack_flamegraph [倍率]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace lesson04 {

constexpr std::size_t kElems = 32ULL * 1024 * 1024;  // 256 MiB
constexpr int kTreeDepth = 8;

std::vector<std::uint64_t> g_table;
std::uint64_t g_scale = 1;

// 热点：在 256 MiB 的表里追指针，每次读写都要等内存。
__attribute__((noinline)) std::uint64_t mix(std::uint64_t seed, std::uint64_t rounds) {
  std::uint64_t idx = seed & (kElems - 1);
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i < rounds; ++i) {
    idx = g_table[idx];
    sum += idx;
  }
  return sum;
}

// 调用链一：handle_query -> parse_header -> decode_request -> mix
// 拿到结果后再做一次运算，避免编译器把调用优化成尾调用、抹掉栈帧。
__attribute__((noinline)) std::uint64_t decode_request(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 5'000'000 * g_scale);
  return r ^ seed;
}

__attribute__((noinline)) std::uint64_t parse_header(std::uint64_t seed) {
  const std::uint64_t r = decode_request(seed ^ 0x9e3779b97f4a7c15ULL);
  return r + seed;
}

__attribute__((noinline)) std::uint64_t handle_query(std::uint64_t seed) {
  const std::uint64_t r = parse_header(seed + 1);
  return r ^ seed;
}

// 调用链二：handle_render -> blend_layers -> mix
__attribute__((noinline)) std::uint64_t blend_layers(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 3'000'000 * g_scale);
  return r + seed;
}

__attribute__((noinline)) std::uint64_t handle_render(std::uint64_t seed) {
  const std::uint64_t r = blend_layers(seed + 2);
  return r ^ seed;
}

// 调用链三：handle_index -> walk_tree 递归 8 层，每层都调一次 mix
__attribute__((noinline)) std::uint64_t walk_tree(std::uint64_t seed, int depth) {
  if (depth == 0) {
    return mix(seed, 250'000 * g_scale);
  }
  const std::uint64_t child =
      walk_tree(seed * 6364136223846793005ULL + 1442695040888963407ULL, depth - 1);
  return child ^ mix(seed ^ child, 250'000 * g_scale);
}

__attribute__((noinline)) std::uint64_t handle_index(std::uint64_t seed) {
  const std::uint64_t r = walk_tree(seed + 3, kTreeDepth);
  return r + seed;
}

}  // namespace lesson04

int main(int argc, char** argv) {
  if (argc > 1) {
    lesson04::g_scale = std::strtoull(argv[1], nullptr, 10);
  }
  if (lesson04::g_scale == 0) {
    lesson04::g_scale = 1;
  }

  // 256 MiB，远大于测试环境 36 MiB 的 L3
  lesson04::g_table.resize(lesson04::kElems);
  const std::uint64_t mask = lesson04::kElems - 1;
  for (std::size_t i = 0; i < lesson04::kElems; ++i) {
    // 打乱成一条不重复的链，让每次访问都跳到随机位置
    lesson04::g_table[i] = (i * 1103515245ULL + 12345ULL) & mask;
  }

  // 种子取自运行时变量，避免编译器按常量把入口函数特化成副本
  const std::uint64_t seed = 0x1234 ^ lesson04::g_scale;
  const auto begin = std::chrono::steady_clock::now();
  const std::uint64_t a = lesson04::handle_query(seed);
  const std::uint64_t b = lesson04::handle_render(seed + 0x4444);
  const std::uint64_t c = lesson04::handle_index(seed + 0x8888);
  const auto end = std::chrono::steady_clock::now();

  const double seconds = std::chrono::duration<double>(end - begin).count();
  std::cout << "query  = " << a << "\n"
            << "render = " << b << "\n"
            << "index  = " << c << "\n"
            << "elapsed: " << seconds << " s\n";
  return 0;
}
