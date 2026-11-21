# 05 采样到行：热点到底在哪一行

前面已经确认程序在等内存，也知道它在等哪一类东西。
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
perf report --stdio -i perf.data --no-children -g none
```

要看的只有一件事：报告里有一个函数占了九成。
接着加 `--sort srcline`，同一个占比就落到某一行。

## 一、示例程序

> 不关心程序的具体逻辑没关系，可以直接跳到 [二、那条命令](#二那条命令)。

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
瓶颈落在内存带宽上，单次延迟的影响很小。
`chase` 就是等内存那一课的指针追逐，地址依赖上一次的结果，只能串行等。

拆成两个函数，是因为行级分析要求一行代码只做一件事。
两种访问方式写进同一个循环，编译后的指令会交错，
采样容易落到相邻语句上，行号就和源码对不上。

数组填充和链表构造沿用等内存那一课，这里不再解释。

## 二、那条命令

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 -g -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line
```

五个参数各管一件事：

| 参数 | 作用 |
|---|---|
| `-e cycles:ppp` | 采硬件周期数；`ppp` 是精确采样，让样本记在真正触发事件的那条指令上 |
| `-F 999` | 每秒采约 999 次，数字越大越精细，开销也越大 |
| `-g` | 同时记下调用栈 |
| `-o perf.data` | 采样结果写进这个文件；不写也默认叫 `perf.data` |
| `taskset -c 2` | 绑到 P 核，避免大小核迁移 |

`taskset`、`-e cycles` 的写法前面已经讲过，这里只多了一个 `ppp` 后缀。
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
perf report --stdio -i perf.data --no-children -g none
```

前面几个参数先说明：

| 参数 | 作用 |
|---|---|
| `--stdio` | 用纯文本打印，不进交互界面，方便直接抄进笔记 |
| `-i perf.data` | 指定读哪个采样文件，名字就是上一步 `-o` 写的那个 |
| `--no-children` | 只算函数自身的时间，不把子函数的时间累加进来 |
| `-g none` | 不打印调用链 |

不加 `--stdio` 时 `perf report` 会打开一个全屏界面，用方向键浏览，内容一样。

`-g none` 值得单独说一句。采样时用了 `-g`，报告默认会把调用栈也打印出来，
每个函数下面多出几行 `|`、`---`，一直列到 `_start`，中间还夹着内核地址。
这就是调用栈，照着读能得到"谁调用了谁"，但本课只想看函数占比，先关掉它。
后面讲调用关系时会再把它打开。

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

这份报告分三层看：表头、列名、数据行。

### 表头两行

- `# Samples: 1K`：一共采到约 1000 个样本，`K` 表示千；
- `of event 'cpu_core/cycles/ppp'`：样本统计的是 P 核周期数，和采样命令里的 `-e` 对应；
- `# Event count (approx.)`：把样本按比例换算回硬件计数，约 53 亿个周期。
  带 `approx.` 是因为样本本身是抽样，只能估算。

### 四个列名

| 列 | 含义 |
|---|---|
| `Overhead` | 该函数占全部样本的比例，可以当成占总耗时的比例 |
| `Command` | 进程名；这里只有一个进程，所以每行都一样 |
| `Shared Object` | 代码在哪个二进制里：`05_hot_line` 是程序自身，`libc.so.6` 是系统 C 库 |
| `Symbol` | 函数名；`[.]` 表示用户态代码，`[k]` 表示内核代码 |

`chase(...)` 的括号里原本是 `<unsigned long, ...>` 这类模板参数，太长，这里省略了。

### 数据行怎么读

第一行 `chase` 占 90.53%，意思是约九成的采样落在这个函数里。

同样迭代 1000 万次，`scan` 连 0.4% 都不到，干脆没进报告。
两个函数耗时差了两个数量级，瓶颈就在 `chase`。

另外三行可以忽略：

- `main` 负责填充数组和计时，本身没多少计算；
- `__memset_avx2_unaligned_erms` 是第一次触碰 256 MiB 内存时清零页面的开销；
- `[k] 0xffff...` 是内核地址没解析出符号，量太小，不用追。

它们都算不上程序的热点，却是后面几课常见的干扰项。

## 四、再看行

`perf report` 默认按函数汇总。加一个排序参数，第一列就换成源码行：

```bash
perf report --stdio -i perf.data --sort srcline --no-children -g none --percent-limit 0.5
```

四个参数的含义：

- `--sort srcline`：指定汇总维度。`srcline` 是"源码文件加行号"，第一列因此变成 `main.cpp:27` 这种形式；
- `--no-children`：只算这一行自身采样到的开销，不把子函数的时间累加上来；
- `-g none`：和上一节一样不打印调用链。采样时用了 `-g`，不加这个参数，
  每行下面会跟着一串 `|`、`---` 的调用栈，一直列到 `_start`，报告会变长好几倍；
- `--percent-limit 0.5`：只显示占比不低于 0.5% 的行。
  不加这个参数时，所有采到样本的行都会列出来。

排序字段可以叠加，用逗号分隔，按书写顺序分层。

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

每次采样的落点不同，占比会有零点几个百分点的浮动，数值不必和这里完全一致。

和上一节比，报告的整体结构没变，变的是列和粒度。

### 表头没变

`# Samples: 1K of event 'cpu_core/cycles/ppp'` 和上一节一样。
读的是同一个 `perf.data`，采的是同一批样本，只是换了汇总方式。
样本数、事件名都不会因为排序方式而改变。

### 列名换了

上一节的四列 `Command`、`Shared Object`、`Symbol`，来自 `perf report` 的默认排序字段 `comm,dso,symbol`。
`--sort` 是整组替换，不是追加，所以这三列一起消失，只剩下面两列：

| 列 | 含义 |
|---|---|
| `Overhead` | 该行占全部样本的比例，可以当成这行代码占总耗时的比例 |
| `Source:Line` | 源码文件名加行号，如 `main.cpp:27`；行号来自编译时加的 `-g` |

想把进程名也留下来，写成 `--sort comm,srcline` 即可，逗号左边先分层。

### 数据行

只有五行，是因为命令里加了 `--percent-limit 0.5`，占比低于 0.5% 的行被隐藏了。
`scan` 的循环因此一行都没出现。

逐行对照源码：

| 行 | 占比 | 对应代码 |
|---|---|---|
| `main.cpp:27` | 89.67% | `chase` 里的 `idx = data[idx];`，指针追逐的那次读取 |
| `??:0` | 5.87% | 内核样本，行号解析不出，环境限制，见附录第 2 条 |
| `memset-vec-unaligned-erms.S:328` | 1.56% | `libc` 的 `memset`，第一次触碰 256 MiB 时清零页面 |
| `main.cpp:44` | 1.05% | `main` 里填充数组的循环体 |
| `main.cpp:42` | 0.58% | 同一个填充循环的循环头 |

除第一行外都可以忽略：内核和 `memset` 是环境开销，`main.cpp:42`、`44` 是初始化，不是程序真正的计算。

第一行才是重点。上一节按函数看，`chase` 占 90.53%；这里细化到行，同一段代码的一行占 89.67%。
两个数字几乎相等，说明 `chase` 的耗时没有被摊到其他行，九成时间就压在这一条读取指令上。
这就是把粒度从函数细化到行的意义：**知道慢在哪个函数，还要知道慢在函数里的哪一句。**

`main.cpp:27` 就是 [chase 里的 `idx = data[idx];`](main.cpp#L27-L27)，一行代码承担了九成时间，这就是"热点"。

这一行只做一件事：从 `data + idx * 8` 读 8 个字节。
真正花时间的是"读之前不知道地址"：

1. 上次读到的值就是这次的下标，CPU 没法提前发请求；
2. 地址在 256 MiB 里随机跳，L3 命中率很低；
3. 每次未命中要等几十到上百个周期，期间流水线没事做。

`scan` 也在读内存，但地址连续、可以预取，不等单次延迟。

如果想再往下看一条指令级，用 `perf annotate`，见附录第 5 条。

## 五、本节收获

- `perf record` 采样，`perf report` 汇总，两者分工明确；
- 默认按函数汇总，`--sort srcline` 换成按源码行汇总；
- 热点行要和源码对照看，再结合等内存那一课的指标判断它在等什么；
- 行级分析前，让每行代码只做一件事，否则编译器重排会让行号失真；
- 报告里的 `main`、`memset`、`[unknown]` 常是干扰项，先确认绝对占比。

到这里能回答"哪一行"，但回答不了"谁调用了它"。
接下来把采样按调用关系摊开。

## 附：跑不出结果时

**1. `cycles:ppp` 显示 `<not supported>`**

有的 CPU 或虚拟机不支持精确采样。换成软件事件，其余参数不变：

```bash
taskset -c 2 perf record -e cpu-clock -F 999 -g -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line
```

仍能看到热点函数，只是行号可能偏一条指令。

顺带记一个相关的坑：`perf record` 不带 `-e` 时，在大小核机器上可能默认选了 E 核
（`cpu_atom`）的事件，而程序大多跑在 P 核上，样本会少得可怜。
本课显式写 `-e cycles:ppp` 就是为了避开它；`cpu-clock` 则不分核心类型。

**2. 报告里出现 `??:0` 或 `[k] 0xffffffff...`**

内核里的样本。`/proc/kallsyms` 受权限限制，perf 解析不出符号，于是显示成地址。
本课它们只占几个百分点，忽略即可。

报告头部还会打一段 `Kernel address maps were restricted` 的警告，说的就是这件事。
不影响用户态的分析，不用处理。

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
说明耗时确实来自这条加载指令，与循环判断无关。
如果热点出现在 `movq` 的下一条，说明采样没精确归因，回到附录第 1 条。
