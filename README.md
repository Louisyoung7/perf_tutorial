# perf_tutorial

Learn Linux perf the hard way — by writing C++ programs that are meant to be profiled.

每课一个目录，目录里是可编译、可运行的示例程序，README 记录完整分析过程。

## 构建

依赖：CMake >= 3.16、支持 C++20 的编译器、Linux `perf`。

```bash
cmake -S . -B build
cmake --build build -j
```

所有目标统一使用 `-O2 -g -fno-omit-frame-pointer`，保证 perf 能解析符号、采样栈完整。

## 本机环境

各课 README 里的实测输出都来自下面这台机器，对照自己的数字前先确认环境是否一致：

| 项 | 值 |
|---|---|
| 发行版 | Ubuntu 24.04.5 LTS |
| 内核 | Linux 7.0.0-43-generic，x86_64 |
| CPU | 13th Gen Intel Core i9-13980HX |
| 线程 / 核心 | 32 线程，24 核（8 P 核 + 16 E 核） |
| 核心布局 | CPU 0-15 为 P 核（每核 2 线程），16-31 为 E 核 |
| 缓存 | L1d 896 KiB，L1i 1.3 MiB，L2 32 MiB，L3 36 MiB |
| 内存 | 30 GiB，单 NUMA 节点 |
| 编译器 | g++ 13.3.0（CMake 实际使用 `/usr/bin/c++`） |
| CMake | 3.28.3 |
| perf | 7.0.14 |
| `perf_event_paranoid` | 1 |
| 构建配置 | Release，C++20，`-O2 -g -fno-omit-frame-pointer` |

自查命令：

```bash
lscpu | grep -E 'Model name|^CPU\(s\)|Thread|Core|L[123]'
free -h | head -2
g++ --version | head -1
cmake --version | head -1
perf --version
cat /proc/sys/kernel/perf_event_paranoid
```

换一台机器，第 01、02 课里的绝对计数和耗时都会变；比值（IPC、未命中率）和结论仍然适用。

## 课程

| 课 | 主题 | 示例程序 |
|---|---|---|
| [01](lessons/01_environment/README.md) | 环境自检与构建跑通 | `01_environment` |
| [02](lessons/02_stat_metrics/README.md) | 读懂 perf stat 的六个数字 | `02_stat_metrics` |

## 新增一课

1. 新建 `lessons/NN_topic/`，放入 `CMakeLists.txt`、源码、`README.md`；
2. 在根 [CMakeLists.txt](CMakeLists.txt) 追加一行 `add_subdirectory(lessons/NN_topic)`；
3. 不改动已有课程。
