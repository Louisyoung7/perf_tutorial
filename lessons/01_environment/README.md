# 01 先把程序跑起来

这一课只做一件事：让示例程序在这台机器上编译、运行，并确认 perf 已经装好。

## 最短路径

```bash
perf --version
cmake -S . -B build
cmake --build build -j
./build/lessons/01_environment/01_environment
```

看到 `spin(...) = ...` 和 `elapsed: ... s` 两行，这一课就完成了。
跑不通时，直接跳到文末 [附：跑不出结果时](#附跑不出结果时)。

## 一、示例程序

> 不关心程序的具体逻辑没关系，这段代码只为跑起来，可以直接跳到 [二、构建与运行](#二构建与运行)。

[main.cpp](main.cpp) 只有一个函数 `spin`，做 30 亿次整数乘加，跑 1 秒左右。

两个刻意的设计：

1. 加 `noinline`，避免被内联，后面的课才能在报告里单独看到 `spin`；
2. 次数从命令行传，做对照实验时不用重新编译。

代码本身没有技术含量，它的作用只是"有一个稳定的、纯计算的热点"。

## 二、构建与运行

```bash
cmake -S . -B build
cmake --build build -j
./build/lessons/01_environment/01_environment
```

实际输出：

```
spin(3000000000) = 6908886848337831168
elapsed: 0.908327 s
```

为什么不直接写 `g++ main.cpp`：

- 仓库里每课一个可执行文件，目标多，逐个手写编译命令容易出错；
- 编译选项统一写在根 [CMakeLists.txt](../../CMakeLists.txt) 里，
  目前是 `-O2 -g -fno-omit-frame-pointer`。

其中 `-g` 是给 perf 用的：没有它，报告里只剩地址，看不到函数名和行号。
`-O2` 让程序像真实发布版本一样被优化，避免带着调试版的慢代码去测。

记下 `elapsed`：程序跑得越久，下一个课采样拿到的样本越多。

## 三、确认 perf 装好了

```bash
perf --version
```

实际输出：

```
perf version 7.0.14
```

perf 是 Linux 内核自带的性能分析工具，通常随发行版的 `linux-tools` 一起装。
它靠 CPU 里的性能计数器（PMU）工作，不用改代码、不用重新编译，也不用在程序里埋点。

再确认一次权限：

```bash
cat /proc/sys/kernel/perf_event_paranoid
```

实际输出：

```
1
```

取值为 1 或更小才能读到硬件计数器。
大于 1 就采不到样本，处理办法见附录第 3 条。

### 三个子命令，先认个脸

| 子命令           | 做什么                           | 回答什么问题       |
| ------------- | ----------------------------- | ------------ |
| `perf stat`   | 运行程序，数各类事件的**总数**             | 快不快，慢在哪一类    |
| `perf record` | 运行程序，按频率**采样**，存进 `perf.data` | 时间花在哪个函数、哪一行 |
| `perf report` | 读 `perf.data`，按口径汇总成表         | 采样结果怎么看      |

一条关键分工：`record` 只负责采、不解释，`report` 只负责看、不再跑程序。
所以同一份 `perf.data` 可以反复换参数看，不用重新采样。

## 四、本节收获

- 构建只要两条 cmake 命令，运行就是直接执行产物；
- 编译选项里的 `-g` 是 perf 能显示函数名和行号的前提；
- `perf --version` 能出版本、`perf_event_paranoid <= 1`，环境就算就绪；
- `stat` 看总数、`record` 采样、`report` 看结果，后两个是采与看的分工。

接下来才真正用 perf，先读 `perf stat` 报出来的数字。

## 附：跑不出结果时

**1. `cmake` 报找不到编译器，或者 C++17 不支持**

本教程要求 g++ 7 以上（测试环境是 13.3.0）。用下面两条命令确认：

```bash
g++ --version | head -1
cmake --version | head -1
```

**2. `perf` 命令不存在**

```bash
sudo apt install linux-tools-common linux-tools-$(uname -r)
```

装完再跑一次 `perf --version`。

**3. `perf_event_paranoid` 大于 1**

先看当前值：

```bash
cat /proc/sys/kernel/perf_event_paranoid
```

要改成 1 需要系统级权限，测试环境的初始值是 4：

```bash
sudo sysctl -w kernel.perf_event_paranoid=1
```

这一步是改内核参数，确认自己有权限再执行。

**4. 程序跑得太快或太慢**

第一个参数是迭代次数，默认 30 亿次约 1 秒：

```bash
./build/lessons/01_environment/01_environment 1000000000
```

跑 1 秒左右最合适：太短采不到样本，太长浪费等待时间。
