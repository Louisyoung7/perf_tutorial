# 01 先把程序跑起来，让 perf 看到它

## 最短路径

一条条照着敲，跑通了再往下读解释。

```bash
# 1. 构建
cmake -S . -B build
cmake --build build -j

# 2. 运行
./build/lessons/01_environment/01_environment

# 3. 看统计
perf stat ./build/lessons/01_environment/01_environment

# 4. 看热点
perf record -e cpu-clock -F 999 -g -o perf.data ./build/lessons/01_environment/01_environment
perf report --stdio -i perf.data --no-children
```

看到 `spin` 占了九成，这一课就完成了。
命令跑不通时，直接跳到文末 [附：跑不出结果时](#附跑不出结果时)。

## 一、perf 是什么

perf 是 Linux 内核自带的性能分析工具，随发行版的 `linux-tools` 一起安装。

它不需要改代码、不需要重新编译，也不用在程序里埋点。
原理是 CPU 里有性能计数器（PMU），能数周期、数指令、数缓存访问；
内核负责记账，perf 负责把账读出来给你看。

perf 有很多子命令，重点只需记住三个：

| 子命令 | 做什么 | 回答什么问题 |
|---|---|---|
| `perf stat` | 运行程序，统计整段时间里各类事件的**总数** | 快不快，慢在哪一类 |
| `perf record` | 运行程序，按固定频率**采样**，结果存进 `perf.data` | 时间花在哪个函数、哪一行 |
| `perf report` | 读 `perf.data`，按不同口径汇总成表格 | 采样结果怎么看 |

关键的一条分工：

- `record` 只负责采，不解释；
- `report` 只负责看，不再跑程序。

所以同一份 `perf.data` 可以反复 `report`，换参数换口径，不需要重新采样。

另外三个子命令本教程也会碰到，但不会单独讲：

| 子命令 | 用途 | 出现在 |
|---|---|---|
| `perf top` | 类似 `top` 的实时热点监控，适合先扫一眼 | 附录 |
| `perf annotate` | 看到汇编级别的采样 | 第 05 课附录 |
| `perf script` | 把样本导成文本，交给别的脚本 | 第 07 课 |

本课先让 `stat` 和 `record` + `report` 跑起来，第 02 课开始逐个细读。

## 二、示例程序

[main.cpp](main.cpp)
只有一个函数 `spin`，做 30 亿次整数乘加，跑 1 秒左右。

两个刻意的设计：

1. 加 `noinline`，避免被内联，`perf report` 里能单独看到 `spin`。
2. 次数从命令行传，做对照实验时不用重新编译。

## 三、编译运行

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

## 四、先看统计：perf stat

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

## 五、再看热点：perf record + report

```bash
perf record -e cpu-clock -F 999 -g -o perf.data \
  ./build/lessons/01_environment/01_environment
perf report --stdio -i perf.data --no-children
```

`perf record` 的选项：

- `-e cpu-clock`：用软件事件采样，所有机器都能用；
- `-F 999`：每秒采样约 999 次；
- `-g`：同时记下调用栈，`report` 里才能看到"谁调用了 `spin`"；
- `-o perf.data`：采样结果写到这个文件，不写就默认叫 `perf.data`；
- 最后一行是要测的程序，perf 负责启动它、采样、等它结束。

`perf report` 的选项：

- `--stdio`：纯文本输出，不打开交互界面，方便直接贴进文档；
- `-i perf.data`：读哪个采样文件，和 `record` 的 `-o` 对应；
- `--no-children`：每个函数只算自己的开销，不把被调函数的开销累加上去。

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

## 六、本节收获

- `perf stat` 用来看整体计数；
- `perf record` 只负责采样、`perf report` 只负责汇总，一份数据可以反复看；
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

测试环境的初始值是 4，当时的实际输出是：

```
   <not supported>      cpu_core/cpu-cycles/u
   <not supported>      cpu_atom/cpu-cycles/u

       0.933973721 seconds time elapsed
```

**2. 样本只有十几个**

测试环境的 CPU 是大小核架构：性能核叫 P 核，能效核叫 E 核。
两类核心的硬件计数器是分开的，perf 里分别叫 `cpu_core` 和 `cpu_atom`。
`perf record` 不带 `-e` 时默认选了 `cpu_atom` 的事件，
而程序大多跑在 P 核上，采到的样本自然少。
按本课命令显式加 `-e cpu-clock -F 999` 即可，`cpu-clock` 不区分核心类型。

**3. 程序跑得太快**

把迭代次数调到 1 亿，只跑约 32 毫秒，样本会少到没有参考价值。
先用默认的 30 亿次。
