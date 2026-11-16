# 03 采样到行：热点到底在哪一行

第 02 课看到 `cache` 模式的 IPC 只有 0.16，结论是"在等内存"。
这一课回答下一个问题：**等在哪一行代码上？**

工具从 `perf stat` 换成 `perf record` + `perf report`。
做法是写一个程序，里面有两个访问方式不同、迭代次数相同的函数，
再用采样看时间落在谁身上、落在哪一行。

## 一、示例程序

源码：[main.cpp](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/03_hot_line/main.cpp)

```bash
./build/lessons/03_hot_line/03_hot_line [迭代次数]
```

程序准备一个 256 MiB 的数组，先做两件事：

| 函数 | 做什么 | 预期 |
|---|---|---|
| `scan` | 从头到尾顺序读 1000 万个元素 | 很快 |
| `chase` | 每次读到的值决定下一次读哪里 | 很慢 |

两个函数都是 `noinline`，都会把结果打印出来，和第 02 课的处理一样。

### 1. scan：地址连续

```cpp
for (std::uint64_t i = 0; i < n; ++i) {
  sum += data[i];
}
```

每次读的地址是上一个地址加 8，硬件预取器能提前把后面的缓存行取进来。
CPU 不需要停下来等某一次读取，限制它的是内存带宽，不是延迟。

### 2. chase：地址来自上一次的结果

```cpp
for (std::uint64_t i = 0; i < n; ++i) {
  idx = data[idx];
  sum += idx;
}
```

`idx` 存的是"下一个下标"，所以下一次读哪里，必须先读到这一次的值才知道。
这条依赖链没法并行，只能一次一次等。
数组 256 MiB 又远大于本机 36 MiB 的 L3，大多数访问要走到内存。

### 3. 为什么拆成两个函数

行级分析要求一行代码只做一件事。

两种访问方式写进同一个循环时，编译后的指令会交错，采样容易落到相邻语句上，行号就和源码对不上。
拆成两个函数后，每个循环只有一种访问方式，行号才可信。

数组填充和指针链的构造沿用第 02 课的做法，这里不再重复解释。

## 二、固定采样命令

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 -g -o perf.data \
  ./build/lessons/03_hot_line/03_hot_line
```

和第 01 课相比，只换了事件名：

- `cycles:ppp`：采硬件周期数。
  后面的 `ppp` 是精确采样的级别，让样本尽量记在真正触发事件的那条指令上。
  去掉它也能跑，但热点常常会记到慢指令的**下一条**，行号偏一行。
- `-F 999`：每秒采约 999 次，和第 01 课一致。
- `-g`：记录调用栈。
- `taskset -c 2`：绑到 P 核，避免大小核迁移，和第 02 课一致。

本机是大小核架构，perf 会把 `cycles` 映射到 P 核的事件，
所以报告表头显示的是 `cpu_core/cycles/ppp`。

实际运行约 0.94 秒，采到 1000 多个样本：

```
scan(10000000) = 167772244842560
chase(10000000) = 167759082871360
elapsed: 0.944513 s
[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 0.108 MB perf.data (1071 samples) ]
```

## 三、先看函数：时间在谁身上

```bash
perf report --stdio -i perf.data --no-children
```

实际输出（只留程序自身的函数）：

```
# Samples: 1K of event 'cpu_core/cycles/ppp'
#
# Overhead  Command      Shared Object      Symbol
# ........  ...........  .................  .................................
#
    89.97%  03_hot_line  03_hot_line        [.] chase(...)
     1.55%  03_hot_line  03_hot_line        [.] main
     0.45%  03_hot_line  03_hot_line        [.] scan(...)
```

同样迭代 1000 万次，`chase` 占 89.97%，`scan` 只占 0.45%。
差了约 200 倍，和两个循环的写法完全对得上。

另外两行不用管：

- `main` 是填充数组、计时这些准备工作；
- `__memset_avx2_unaligned_erms` 是内核在第一次触碰 256 MiB 内存时清零页面。

## 四、再看行：时间在 `chase` 的哪一行

`perf report` 默认按函数汇总。
加一个排序参数，就把第一列换成源码行：

```bash
perf report --stdio -i perf.data --sort srcline --no-children
```

实际输出：

```
# Samples: 1K of event 'cpu_core/cycles/ppp'
#
# Overhead  Source:Line
# ........  ...............................
#
    89.49%  main.cpp:27
     5.70%  ??:0
     2.29%  memset-vec-unaligned-erms.S:328
     0.82%  main.cpp:44
     0.73%  main.cpp:42
```

`main.cpp:27` 就是 [chase 里的 `idx = data[idx];`](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/03_hot_line/main.cpp#L27-L27)。

对照源码看：

- 第 27 行，指针追逐的读取，89.49%；
- 第 26 行，循环开始，几乎为 0；
- `scan` 的整个循环没有出现在报告里，占比低于 0.5%；
- 第 42、44 行在 `main` 里，是数组的填充循环，合计不到 2%。

一行代码承担了九成时间，这就是"热点"。

`??:0` 是内核地址解析不出来的样本，属于采样环境限制，不是程序热点，见附录第 2 条。

## 五、为什么是这一行

第 27 行只做一件事：从 `data + idx * 8` 读 8 个字节。
真正花时间的，是读之前不知道地址：

1. 上一次读到的值就是这一次的下标，CPU 没法提前发出请求；
2. 地址在 256 MiB 范围内随机跳，L3 命中率很低；
3. 每次未命中要等几十到上百个周期，期间流水线没有别的事可做。

`scan` 的第 16 行也在读内存，但地址连续、可以预取，不等单次延迟。
所以两个函数迭代次数一样，时间却差了两个数量级。

如果想再往下看到指令级，用 `perf annotate`，见附录第 5 条。

## 六、本节收获

- `perf record` 负责采样，`perf report` 负责汇总；
- 默认按函数汇总，加 `--sort srcline` 就按源码行汇总；
- 热点行要和源码对照看，再结合第 02 课的知识判断它在等什么；
- 行级分析前，让每行代码只做一件事，否则编译器重排会让行号失真；
- 采样是事后归因，`cycles:ppp` 能把样本钉在真正变慢的指令上。

下一课把采样按调用关系摊开，看热点是被谁调用的。

## 附：跑不出结果时

**1. `cycles:ppp` 显示 `<not supported>`**

有的 CPU 或虚拟机不支持精确采样。
把事件换成第 01 课的 `-e cpu-clock`，其余参数不变：

```bash
taskset -c 2 perf record -e cpu-clock -F 999 -g -o perf.data \
  ./build/lessons/03_hot_line/03_hot_line
```

这样仍能看到热点函数，但行号可能偏一条指令。

**2. 报告里出现 `??:0` 或 `[k] 0xffffffff...`**

这些是内核里的样本。
本机 `/proc/kallsyms` 有权限限制，perf 解析不出内核符号，于是显示成地址。
它们通常只占几个百分点，忽略即可。

**3. 样本只有十几条**

程序跑得太短。默认 1000 万次迭代约 0.94 秒，能采到 1000 个左右样本。
如果改过迭代次数，调回默认值或加大：

```bash
./build/lessons/03_hot_line/03_hot_line 30000000
```

**4. 想改运行时长**

第一个参数就是迭代次数，`scan` 和 `chase` 会一起变：

```bash
taskset -c 2 ./build/lessons/03_hot_line/03_hot_line 20000000
```

**5. 想看指令级：perf annotate**

```bash
perf annotate --stdio -i perf.data
```

输出是一段汇编，每行前面是采样占比。`chase` 的核心几行实际是这样的：

```
   99.81 :   14d8:        movq    (%rdi,%rax,8), %rax
    0.00 :   14dc:        addq    $0x1, %rdx
    0.09 :   14e0:        addq    %rax, %rcx
```

`movq` 就是第 27 行的那次读取，99.81% 的样本落在它身上。
说明第 27 行的耗时确实来自这条加载指令，不是循环判断或其他代码。

如果这里看到的热点是在 `movq` 的下一条，说明采样没有精确归因，回到附录第 1 条。
