# 05 采样到行：热点到底在哪一行

第 03 课知道程序在等内存，第 04 课知道它在等什么类型的东西。
这一课只回答一个问题：**等在哪一行代码上？**

工具从 `perf stat` 换成 `perf record` + `perf report`。

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 运行
./build/lessons/05_hot_line/05_hot_line

# 3. 采样（约 1 秒）
taskset -c 2 perf record -e cycles:ppp -F 999 -g -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line

# 4. 看结果
perf report --stdio -i perf.data --no-children
```

要看的只有一件事：报告里有一个函数占了九成。
接着加 `--sort srcline`，同一个占比就落到某一行。

## 一、示例程序

[main.cpp](main.cpp) 里有两个函数，迭代次数相同：

| 函数 | 访问方式 | 预期 |
|---|---|---|
| `scan` | 从头到尾顺序读 1000 万个元素 | 很快 |
| `chase` | 每次读到的值决定下一次读哪里 | 很慢 |

```cpp
// scan：地址连续
for (std::uint64_t i = 0; i < n; ++i) {
  sum += data[i];
}

// chase：地址来自上一次的结果
for (std::uint64_t i = 0; i < n; ++i) {
  idx = data[idx];
  sum += idx;
}
```

`scan` 每次读的地址是上一个加 8，硬件预取器能提前把数据搬进来，
限制它的是内存带宽，不是单次延迟。
`chase` 就是第 03 课的指针追逐，地址依赖上一次的结果，只能串行等。

拆成两个函数，是因为行级分析要求一行代码只做一件事。
两种访问方式写进同一个循环，编译后的指令会交错，
采样容易落到相邻语句上，行号就和源码对不上。

数组填充和链表构造沿用第 03 课，这里不再解释。

## 二、那条命令

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 -g -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line
```

四个参数各管一件事：

| 参数 | 作用 |
|---|---|
| `-e cycles:ppp` | 采硬件周期数；`ppp` 是精确采样，让样本记在真正触发事件的那条指令上 |
| `-F 999` | 每秒采约 999 次 |
| `-g` | 同时记下调用栈 |
| `taskset -c 2` | 绑到 P 核，避免大小核迁移 |

去掉 `ppp` 也能跑，但热点常常记到慢指令的**下一条**，行号会偏一行。

实际运行约 0.96 秒：

```
scan(10000000) = 167772244842560
chase(10000000) = 167759082871360
elapsed: 0.961878 s
[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 0.107 MB perf.data (1060 samples) ]
```

样本数不用管具体多少，几百到几千都够看比例。

## 三、先看函数

```bash
perf report --stdio -i perf.data --no-children
```

实际输出（模板参数已省略，方便看）：

```
# Samples: 1K of event 'cpu_core/cycles/ppp'
# Event count (approx.): 5322047527
#
# Overhead  Command      Shared Object         Symbol
# ........  ...........  ....................  .................................
#
    90.53%  05_hot_line  05_hot_line           [.] chase(...)
     1.63%  05_hot_line  05_hot_line           [.] main
     1.56%  05_hot_line  libc.so.6             [.] __memset_avx2_unaligned_erms
     0.42%  05_hot_line  [unknown]             [k] 0xffffffff9e201268
```

同样迭代 1000 万次，`chase` 占 90.53%，`scan` 低到没进报告（不到 0.4%）。

另外两行不用管：`main` 是填充数组和计时，
`__memset_avx2_unaligned_erms` 是内核第一次触碰 256 MiB 内存时清零页面。
它们都不是程序的热点，却是后面几课常见的干扰项。

## 四、再看行

`perf report` 默认按函数汇总。加一个排序参数，第一列就换成源码行：

```bash
perf report --stdio -i perf.data --sort srcline --no-children
```

两个参数的含义：

- `--sort srcline`：指定汇总维度。`srcline` 是"源码文件加行号"，第一列因此变成 `main.cpp:27` 这种形式；
- `--no-children`：只算这一行自身采样到的开销，不把子函数的时间累加上来。

`-F` 是 `--sort` 的简写，排序字段还可以叠加，按书写顺序分层。

实际输出：

```
# Samples: 1K of event 'cpu_core/cycles/ppp'
#
# Overhead  Source:Line
# ........  ...............................
#
    89.67%  main.cpp:27
     5.87%  ??:0
     1.56%  memset-vec-unaligned-erms.S:328
     1.05%  main.cpp:44
     0.58%  main.cpp:42
```

`main.cpp:27` 就是 [chase 里的 `idx = data[idx];`](main.cpp#L27-L27)。

对照源码看：

- 第 27 行，指针追逐的读取，89.67%；
- 第 26 行，循环开始，几乎为 0；
- `scan` 的整个循环没有出现，占比低于 0.5%；
- 第 42、44 行在 `main` 里，是填充数组，合计不到 2%。

一行代码承担了九成时间，这就是"热点"。

`??:0` 是内核地址解析不出来的样本，属于环境限制，见附录第 2 条。

这一行只做一件事：从 `data + idx * 8` 读 8 个字节。
花时间的不是读，是"读之前不知道地址"：

1. 上次读到的值就是这次的下标，CPU 没法提前发请求；
2. 地址在 256 MiB 里随机跳，L3 命中率很低；
3. 每次未命中要等几十到上百个周期，期间流水线没事做。

`scan` 也在读内存，但地址连续、可以预取，不等单次延迟。

如果想再往下看一条指令级，用 `perf annotate`，见附录第 5 条。

## 五、本节收获

- `perf record` 采样，`perf report` 汇总，两者分工明确；
- 默认按函数汇总，`--sort srcline` 换成按源码行汇总；
- 热点行要和源码对照看，再结合第 03 课的指标判断它在等什么；
- 行级分析前，让每行代码只做一件事，否则编译器重排会让行号失真；
- 报告里的 `main`、`memset`、`[unknown]` 常是干扰项，先确认绝对占比。

到这里能回答"哪一行"，但回答不了"谁调用了它"。
下一课把采样按调用关系摊开。

## 附：跑不出结果时

**1. `cycles:ppp` 显示 `<not supported>`**

有的 CPU 或虚拟机不支持精确采样。换成软件事件，其余参数不变：

```bash
taskset -c 2 perf record -e cpu-clock -F 999 -g -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line
```

仍能看到热点函数，只是行号可能偏一条指令。

**2. 报告里出现 `??:0` 或 `[k] 0xffffffff...`**

内核里的样本。`/proc/kallsyms` 受权限限制，perf 解析不出符号，于是显示成地址。
本课它们只占几个百分点，忽略即可。

**3. 样本只有十几条**

程序跑得太短。默认 1000 万次约 1 秒，能采到 1000 个左右样本。
调大迭代次数：

```bash
./build/lessons/05_hot_line/05_hot_line 30000000
```

**4. 想改运行时长**

第一个参数是迭代次数，`scan` 和 `chase` 一起变：

```bash
taskset -c 2 ./build/lessons/05_hot_line/05_hot_line 20000000
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

`movq` 就是第 27 行的那次读取，样本几乎全落在它身上。
说明耗时确实来自这条加载指令，不是循环判断。
如果热点出现在 `movq` 的下一条，说明采样没精确归因，回到附录第 1 条。
