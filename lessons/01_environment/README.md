# 01 先把程序跑起来，让 perf 看到它

本课只做三件事：

1. 编译并运行示例程序；
2. 用 `perf stat` 看到耗时统计；
3. 用 `perf record` + `perf report` 看到热点函数 `spin`。

指标怎么读、汇编怎么看，都放到后面几课。

命令跑不通时，直接跳到文末 [附：跑不出结果时](#附跑不出结果时)。

## 一、示例程序

[main.cpp](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/01_environment/main.cpp)
只有一个函数 `spin`，做 30 亿次整数乘加，跑 1 秒左右。

两个刻意的设计：

1. 加 `noinline`，避免被内联，`perf report` 里能单独看到 `spin`。
2. 次数从命令行传，做对照实验时不用重新编译。

## 二、编译运行

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

记下 `elapsed`：程序跑得越久，perf 能采到的样本越多。

## 三、先看统计：perf stat

```bash
perf stat ./build/lessons/01_environment/01_environment
```

如果这里只有报错、没有数字，见文末附录第 1 条。

实际输出（只留前几行）：

```
                 6      context-switches
                 2      cpu-migrations
               209      page-faults
            916.13 msec task-clock
     4,145,232,982      cpu_core/cpu-cycles/
    18,161,708,683      cpu_core/instructions/
...
       0.917722563 seconds time elapsed
```

本课只看两处：

- `elapsed` 约 0.92 秒，`task-clock` 约 916 毫秒，说明程序一直在 CPU 上算；
- 其余指标（cycles、instructions、分支、缓存）下一课再讲。

数字每次跑都会有些波动，看数量级即可。
看到有数字输出，就说明 perf 已经能正常工作。

## 四、再看热点：perf record + report

```bash
perf record -e cpu-clock -F 999 -g -o perf.data \
  ./build/lessons/01_environment/01_environment
perf report --stdio -i perf.data --no-children
```

`-e cpu-clock` 指定一个所有机器都能用的软件事件；
`-F 999` 表示每秒采样约 999 次。

如果采集报错，或样本只有十几个，见文末附录第 1、2 条。

实际输出：

```
# Samples: 922  of event 'cpu-clock'
# Event count (approx.): 922922922
#
# Overhead  Command         Shared Object        Symbol
# ........  ..............  ...................  .......................
#
    99.67%  01_environment  01_environment       [.] spin(unsigned long)
            |
            ---spin(unsigned long)
               __libc_start_call_main
               call_init (inlined)
               __libc_start_main@@GLIBC_2.34
               _start
```

只看一行：`99.67%` 落在 `[.] spin(unsigned long)`。
`[.]` 表示用户态函数。

能出现这个名字，本课就完成了。

## 五、本节收获

- `perf stat` 用来看整体计数；
- `perf record` 用来采样，`perf report` 用来看热点；
- 热点函数名能正常显示，说明编译时带的 `-g` 生效了。

下一课开始读 `perf stat` 里的指标。

## 附：跑不出结果时

**1. 报 `Failure to open any events for recording`**

检查权限：

```bash
cat /proc/sys/kernel/perf_event_paranoid
```

值大于 1 就拿不到事件，需要改成 1（系统级操作，自行执行）：

```bash
sudo sysctl -w kernel.perf_event_paranoid=1
```

本机初始值是 4，当时的实际输出是：

```
   <not supported>      cpu_core/cpu-cycles/u
   <not supported>      cpu_atom/cpu-cycles/u

       0.933973721 seconds time elapsed
```

**2. 样本只有十几个**

这台机器的 CPU 是大小核架构：性能核叫 P 核，能效核叫 E 核。
两类核心的硬件计数器是分开的，perf 里分别叫 `cpu_core` 和 `cpu_atom`。
`perf record` 不带 `-e` 时默认选了 `cpu_atom` 的事件，
而程序大多跑在 P 核上，采到的样本自然少。
按本课命令显式加 `-e cpu-clock -F 999` 即可，`cpu-clock` 不区分核心类型。

**3. 程序跑得太快**

把迭代次数调到 1 亿，只跑约 32 毫秒，样本会少到没有参考价值。
先用默认的 30 亿次。
