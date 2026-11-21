# 09 事件选择与精确采样：样本可信吗

05 课按行看热点，隐含一个前提：**样本真的落在那条指令上。**

这个前提并不总成立。同一个程序，不带后缀时热点落在 `main.cpp:20`，
把事件换成 `cycles:ppp` 后回到 `main.cpp:19`（真正等内存的那条读取）。
两次的样本数差不多，差的是"样本记录的是哪个瞬间"。

本课只回答两件事：

1. 样本为什么会落偏（skid），什么时候必须在乎；
2. 样本够不够（`-F` / `-c` 与内核上限）。

事件选择不深入原理。02～04 课已经各自示范过怎么用：量内存用 `cache-misses`，
量分支用 `branch-misses`。第三节留了一张对照表，换问题时查一下即可。

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 同一个程序采两次：一次不加后缀，一次加 ppp（各约 0.9 秒）
taskset -c 2 perf record -e cycles -F 999 -o raw.data \
  ./build/lessons/09_events/09_events
taskset -c 2 perf record -e cycles:ppp -F 999 -o ppp.data \
  ./build/lessons/09_events/09_events

# 3. 按行对比
perf report --stdio -i raw.data --sort srcline --no-children -g none
perf report --stdio -i ppp.data --sort srcline --no-children -g none
```

要看的只有这一行差别：

```
# raw.data                         # ppp.data
    92.68%  main.cpp:20                93.80%  main.cpp:19
```

`main.cpp:20` 是右花括号，对应的是函数收尾的 `retq`，不是真正等内存的指令。
真正等内存的是第 19 行。原因见第二节。
百分比每次会浮动一两个百分点，样本数也会浮动；行号不会变。

## 一、示例程序

[main.cpp](main.cpp) 只有一个阶段，三个函数：

| 函数 | 写法 | 作用 |
| --- | --- | --- |
| `fetch` | 从 64 MiB 的表里读一个元素 | 每次都可能未命中 L3，等几百个周期 |
| `tail` | 一组依赖的乘法和移位 | 本身很快，只用来当"错误落点" |
| `wait_phase` | 1000 万次：`fetch` 一次，`tail` 一次 | 跑约 0.9 秒，够采近千个样本 |

```cpp
__attribute__((noinline)) std::uint64_t fetch(std::uint64_t idx) {
  return g_table[idx];          // 第 19 行，真正等内存的读取
}                               // 第 20 行，收尾只有一条 retq
```

这样安排是为了复现"落点偏一行"：等待发生在第 19 行，
偏掉的样本会落到紧跟其后的收尾指令上。
表是 64 MiB，大于测试环境 36 MiB 的 L3，每次读取都跳到随机位置。

## 二、skid：热点为什么落在下一条

`perf record` 采到的不是"事件发生时的 IP"，而是"CPU 响应中断时的 IP"。
中间隔着几十个周期，处理器已经又执行了一串指令。这段偏差叫 skid（打滑）。

```
周期   0          1 ................ 400       401        402
       movq  ──────── 等数据 ────────→ 完成      retq       ...
                       ↑
                计数器在这里溢出
                       │  中断要等指令边界才送达
                       └──────────────→ 此时 IP 已经指向 retq

非精确采样：样本记在 retq（第 20 行）
精确采样：  样本记在 movq（第 19 行）
```

指令级看得最清楚（单次实测，比例会浮动）：

```bash
perf annotate --stdio -i raw.data --no-source
perf annotate --stdio -i ppp.data --no-source
```

```
# raw.data
    0.00 :   146b:        movq    (%rax,%rdi,8), %rax
   99.87 :   146f:        retq

# ppp.data
   99.87 :   146b:        movq    (%rax,%rdi,8), %rax
    0.13 :   146f:        retq
```

`perf annotate` 的输出每行三段：左边是样本占比，中间是地址，右边是汇编指令。
这里不需要懂汇编，只看占比堆在哪一行就够了。

影响多大，取决于结论粒度：

| 结论粒度 | skid 的影响 |
| --- | --- |
| 函数级占比 | 通常没事，几十条指令多半还在同一函数里 |
| 行级热点 | 可能整体挪到相邻行，甚至落到花括号上 |
| 指令级调优 | 必须消掉，否则会去改一条根本不慢的指令 |

### 加几个 p

`ppp` 让硬件在溢出那一刻把触发事件的指令地址记下来（Intel 上叫 PEBS），
不再依赖中断时机。`p` 的个数表示要求的精确级别，最多三个。测试环境实测（比例会浮动）：

| 写法 | 落点 | 结论 |
| --- | --- | --- |
| `cycles` | `retq` 99.87% | 不精确 |
| `cycles:p` | `retq` 99.78% | 仍不精确 |
| `cycles:pp` | 加载指令 99.68% | 精确 |
| `cycles:ppp` | 加载指令 99.87% | 精确 |

`cycles:pppp` 会直接报错：`Maximum precise value is 3`。
能到哪一级由 CPU 和事件决定，换机器要重新确认落点，不能假定一个 `p` 就够。

一个反直觉的坑：**`perf record` 不带 `-e` 时默认就带精确级别**。
报告头里写的是 `cpu_core/cycles/P`（大写 P 表示 precise_ip=2），
实测约 99% 落在加载指令上。显式写 `-e cycles` 反而把这份默认精确度关掉了。
本课故意这么写，是为了把两种落点分开展示。

软件事件没有这个能力。`cpu-clock` 采同一个程序，样本几乎全落在 `retq` 上（实测 99.8%）；
写成 `cpu-clock:ppp` 也不报错，落点不变。
后缀只是"请求"，硬件不支持时 perf 静默照跑。
判断精确与否，别看后缀，看 `perf annotate` 的落点。

## 三、两个参数，别写错

### 事件：决定测什么

本课用 `cycles`，因为要回答"时间花在哪一行"。换问题时按这张表换事件：

| 想回答 | 用哪个事件 | 单位 |
| --- | --- | --- |
| 程序在 CPU 上待了多久 | `task-clock` | 毫秒 |
| CPU 跑了多少圈 | `cycles` | 周期数 |
| 干了多少活 | `instructions` | 指令条数 |
| 有没有在等内存 | `cache-references`、`cache-misses` | 次 |
| 是不是分支猜错了 | `branches`、`branch-misses` | 次 |

`task-clock` 是毫秒，`cycles` 是个数，两者不能直接比；
它们的比值才是频率，而频率会随负载、温度和核心类型变化。

用 `perf stat` 一次看全（E 核的行先略去）：

```bash
taskset -c 2 perf stat \
  -e task-clock,cycles,instructions,cache-references,cache-misses \
  ./build/lessons/09_events/09_events
```

```
            859.00 msec task-clock
     4,462,465,228      cpu_core/cycles/
       548,239,309      cpu_core/instructions/
        13,160,827      cpu_core/cache-references/
        11,776,473      cpu_core/cache-misses/
```

单次运行的绝对数会随频率和缓存状态浮动，下面的换算以这一次为准。

IPC = 548M / 4.46G ≈ 0.12，平均频率 ≈ 5.2 GHz，末级未命中率 89.5%。
统计包含 64 MiB 表的初始化；用 `./09_events 0` 可量出这部分约 30～50 ms，
扣掉后每次迭代约 430 个周期、34 条指令，IPC 不到 0.1。

混合架构上，通用名 `cycles` 会同时展开到 P 核和 E 核两个 PMU，
所以完整输出里每个事件有两行，没跑的那个核显示 `<not counted>`。
02～04 课显式写 `cpu_core/...`，就是为了消掉这个歧义；绑核和写前缀要一起做。

### 频率：决定采多密

`perf stat` 数全量，`perf record` 抽样，于是有了采多密的问题：

| 写法 | 含义 | 特点 |
| --- | --- | --- |
| `-F 999` | 每秒采约 999 次 | 运行久样本多，运行短样本少 |
| `-c 200000` | 每 200000 个事件采一次 | 样本数与事件数严格成比例 |

测试环境实测（同一个程序，约 0.9 秒；样本数会随运行时长浮动）：

| 参数 | 样本数 | 采样文件 |
| --- | --- | --- |
| `-F 97` | 94 | 0.009 MB |
| `-F 999` | 931 | 0.047 MB |
| `-F 5000` | 4719 | 0.220 MB |
| `-c 200000` | 23180 | 0.889 MB |

为什么平时写 999：跑 1 秒约 1000 个样本，1% 的热点也有 10 个样本。
热点只持续几毫秒，就提高频率；长时间运行，就降到 97 或改用大周期。

`-F` 有上限，写太大不会报错，会被悄悄降下来：

```bash
taskset -c 2 perf record -e cycles:ppp -F 100000 -o hi.data \
  ./build/lessons/09_events/09_events
```

```
warning: Maximum frequency rate (12,000 Hz) exceeded, throttling from 100,000 Hz to 12,000 Hz.
```

上限来自 `/proc/sys/kernel/perf_event_max_sample_rate`。
测试环境里见过 12000、15000、31000、50000 几个值，内核会随采样压力上下调。
换机器、换次运行都可能不同，以警告或该文件为准。
想让它报错而不是降频，加 `--strict-freq`。

## 四、看到可疑数字时的检查清单

1. **表头事件名**：是不是你要的事件？带不带 `/ppp`？
2. **样本数**：只有几十个就先提高 `-F` 或延长运行时间，百分比没有统计意义。
3. **落点**：行级结论用 `perf annotate` 扫一眼，落在 `retq` 或花括号上就是 skid。
4. **大小核**：报告里有没有 `cpu_atom`？没绑核时样本会随迁移分散，横向比较不可靠。

日常照抄 `-e cycles:ppp -F 999` 没问题，因为要回答的就是"时间花在哪一行"。
换问题（内存、分支、系统调用）或换机器时，从第 1 条重新过。

## 附：概念卡片

**PMU**：CPU 里的性能监控单元，一组可编程计数器，`cycles`、`cache-misses` 都由它数出。

**PMI**：计数器溢出时发出的中断；`perf record` 每收到一次就记一个样本。

**skid**：从溢出到中断被处理之间的延迟，期间处理器继续执行，IP 已经走远。

**PEBS / precise_ip**：Intel 的精确采样机制，溢出时由硬件记下触发指令的地址；
`p` 的个数是请求的精确级别（0～3），硬件不支持时 perf 可能降级或报 `<not supported>`。

## 附：跑不出结果时

1. **`cycles:ppp` 显示 `<not supported>`**：换 `cycles:pp`；仍不行就换 `cpu-clock`，但只能看函数级结论。
2. **`warning: Maximum frequency rate ... exceeded`**：正常限流。查 `cat /proc/sys/kernel/perf_event_max_sample_rate`，或加 `--strict-freq`。
3. **样本只有几十个**：用 `-F 5000`，或把倍率调大：`./build/lessons/09_events/09_events 2`。
4. **热点落在右花括号**：skid。改成 `cycles:ppp` 重采；软件事件改不了，只能接受。
