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

## 课程

| 课 | 主题 | 示例程序 |
|---|---|---|
| [01](lessons/01_environment/README.md) | 环境自检与构建跑通 | `01_environment` |

## 新增一课

1. 新建 `lessons/NN_topic/`，放入 `CMakeLists.txt`、源码、`README.md`；
2. 在根 [CMakeLists.txt](CMakeLists.txt) 追加一行 `add_subdirectory(lessons/NN_topic)`；
3. 不改动已有课程。
