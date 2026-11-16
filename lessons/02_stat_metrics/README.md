# 02 读懂 perf stat 的六个数字

第 01 课证明了 perf 能跑。
这一课回答一个问题：`perf stat` 输出的那些大数字，到底和程序做了什么有关系？

做法是写三个小程序，让它们各自制造一种性能特征，再用同一组事件去测：

| mode | 程序在做什么 | 想看到什么 |
|---|---|---|
| `arith` | 40 亿次整数运算 | IPC 高 |
| `cache` | 256 MiB 数组里随机指针追逐 | 缓存未命中率高 |
| `branch` | 随机决定 if 走哪边 | 分支预测错误率高 |

三个内核在同一个文件里：[main.cpp](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/02_stat_metrics/main.cpp)

```bash
./build/lessons/02_stat_metrics/02_stat_metrics [arith|cache|branch] [迭代次数]
```

默认迭代次数已经调到每个约跑 1 秒，一般不用传第二个参数。

## 一、三个内核在做什么

三个内核放在同一个程序里，共用编译选项、同一台机器、同一条命令，只换 mode。
这样对比时变量只有一个：程序的行为。

三个内核都加 `noinline`，不让编译器内联，函数边界清楚；
都把结果 `sum` 打印出来，防止编译器发现"算了没用"而把整个循环删掉。

### 1. arith：制造"没有瓶颈"的状态

```cpp
std::uint64_t sum = 0;
for (std::uint64_t i = 0; i < n; ++i) {
  sum += i * 3 + 1;
}
return sum;
```

循环体只有乘加，数据全在寄存器里：

- 不访问数组，不产生缓存未命中；
- 循环次数固定，分支每次都往同一个方向走。

所以它是本课的**对照组**，代表"CPU 满负荷算数"时能到什么水平。
后面两个内核都拿它当基准：IPC 从 3.98 掉到多少，就是各自付出的代价。

迭代次数取 40 亿，是因为实测约 0.9 秒，和第 01 课的量级一致。

### 2. cache：制造"必须等内存"的访问链

先准备一个 32M 个元素的数组（256 MiB），每个格子存的是**下一个要访问的下标**：

```cpp
const std::uint64_t mask = data.size() - 1;
for (std::size_t i = 0; i < data.size(); ++i) {
  data[i] = (i * 1103515245ULL + 12345ULL) & mask;
}
```

`i * a + c` 再截断，在 `a`、`c` 都取奇数时是一个置换：
每个下标恰好出现一次，等于把整个数组打乱成一条随机链表。

这一段拆开看，只有三件事：

- `mask = data.size() - 1`。
  数组长度是 32M，也就是 2 的 25 次方。
  对 2 的幂做 `x & mask`，等价于 `x % size`，但少一次除法。
- `i * 1103515245ULL + 12345ULL`。
  这就是经典的线性同余公式 `x = x * a + c`，两个常数和 glibc 的 `rand()` 用的是同一组。
  它把连续的下标 `i` 映射成看起来毫无规律的数。
- `& mask` 把结果截到 `[0, 32M)` 范围内。
  截断后的数正好可以当下标用，所以 `data[i]` 里存的是"下一个该去哪"。

为什么这样能得到一条不重复的链？

`a = 1103515245` 是奇数，对 4 取余等于 1；`c = 12345` 是奇数；
模数又是 2 的幂。
这三个条件同时满足时，这个映射在 `0 .. 2^25-1` 上是一个**完整循环**：
每个下标恰好被写入一次，从 0 出发一路迭代会走遍全部 32M 个位置，最后回到 0。
这就是"置换"两个字的实际含义。

换句话说，填完数组后，`data` 就是一张随机打乱的"下一站"表。
它看起来像随机数，实际是确定性、可复现的，并且保证不提前成环。

这一步放在计时之外，只是准备工作。

然后顺着这条链往下走：

```cpp
idx = data[idx];  // 下一次读哪里，取决于这次读到了什么
sum += idx;
```

这个写法叫**指针追逐**。它有两个关键点：

- 地址依赖上一次的结果，CPU 没法提前发出下一个请求；
- 256 MiB 远大于 36 MiB 的 L3，链上的下一站大概率不在缓存里。

于是每次迭代都是"读内存 → 拿到地址 → 再读内存"，一条链串行等下去。
如果改成"随机下标 + 互不依赖的读取"，CPU 可以同时挂起多个未命中的访问，
测出来的时间会明显变短，就达不到本课想看的效果。

数组必须填真实数据。第一版用的是全零数组，结果所有读取都落在同一个共享零页上，
等于全程命中缓存，10M 次只要 0.067 秒。这也是个值得记住的教训：
**内存访问实验里，数据的初始状态会直接改变结论。**

迭代次数取 1000 万，是因为实测一次约 95 纳秒，刚好 1 秒左右。

### 3. branch：制造"猜不对"的分支

```cpp
x ^= x << 13;
x ^= x >> 7;
x ^= x << 17;
if (x & 1) {
  sum = on_odd(sum, x);
} else {
  sum = on_even(sum, x);
}
```

前三行是 xorshift：一种很短的伪随机数生成器，三步移位和异或。
它几乎不消耗时间，唯一作用是让 `x` 的最低位看起来完全随机。

`if (x & 1)` 按这个随机位选分支，下一次走哪边和历史无关，
分支预测器再厉害也只能猜中一半左右。

两个分支都调一个 `noinline` 的小函数，原因很实际：
如果两边都只写一条加减，编译器会把它改写成 `cmov` 这类无分支指令。
第一版就是这样，`branch-misses` 只有 6 万，实验等于没做。
函数调用挡住了这种改写，条件跳转才会真的留在二进制里。

迭代次数取 3 亿，是因为实测一次约 4 纳秒，跑 1.2 秒左右。

## 二、固定一条测量命令

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_metrics/02_stat_metrics arith
```

命令里有两处和第 01 课不同：

1. `taskset -c 2` 把进程绑在 2 号 CPU 上。
   测试环境 0-15 号是 P 核，16-31 号是 E 核。
   不绑核，进程可能在大小核之间迁移，前后两组数字没法比。
2. 事件名前加 `cpu_core/`，只数 P 核的计数器。
   第 01 课附录讲过，默认事件会把 `cpu_atom` 也混进来。

六个事件的含义：

| 事件 | 含义 |
|---|---|
| `cycles` | 处理器时钟周期数 |
| `instructions` | 执行完的指令数 |
| `branches` | 分支指令数 |
| `branch-misses` | 分支预测失败的次数 |
| `cache-references` | 访问末级缓存的次数 |
| `cache-misses` | 末级缓存没命中的次数 |

原始数字本身没意义，要换算成三个比值：

- **IPC** = `instructions / cycles`，每个周期执行几条指令；
- **缓存未命中率** = `cache-misses / cache-references`；
- **分支预测错误率** = `branch-misses / branches`。

下面三次实验都只看这三个比值。

## 三、实验一：纯计算，IPC 拉满

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_metrics/02_stat_metrics arith
```

实际输出：

```
arith(4000000000) = 5553255924290448384
elapsed: 0.890184 s
     4,037,784,868      cpu_core/cycles/
    16,064,977,044      cpu_core/instructions/
     4,010,946,857      cpu_core/branches/
            59,436      cpu_core/branch-misses/
           251,747      cpu_core/cache-references/
            84,776      cpu_core/cache-misses/
```

三个比值：

- IPC = 160.6 亿 / 40.4 亿 ≈ **3.98**；
- 分支预测错误率 = 5.9 万 / 40.1 亿 ≈ **0.0015%**；
- 缓存未命中率 = 8.5 万 / 25.2 万 ≈ 34%，但总共只有 25 万次访问。

结论：循环次数固定，分支每次都能预测对；数据只有几个寄存器，不碰内存。
所以每个周期能执行近 4 条指令，瓶颈不在分支也不在内存。

注意最后一行的 34%：比率高，但绝对次数少到可以忽略。
看指标要同时看比率和次数。

## 四、实验二：随机访存，等内存

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_metrics/02_stat_metrics cache
```

实际输出：

```
cache(10000000) = 167759082871360
elapsed: 0.931109 s
     4,843,560,323      cpu_core/cycles/
       752,481,137      cpu_core/instructions/
       123,964,951      cpu_core/branches/
            85,674      cpu_core/branch-misses/
        20,631,243      cpu_core/cache-references/
        18,739,347      cpu_core/cache-misses/
```

三个比值：

- IPC = 7.5 亿 / 48.4 亿 ≈ **0.16**；
- 缓存未命中率 = 1874 万 / 2063 万 ≈ **90.8%**；
- 分支预测错误率 ≈ **0.07%**。

结论：每次访问的地址来自上一次读到的值，硬件没法提前预取。
256 MiB 的数组远大于 36 MiB 的 L3，九成访问都要去内存拿。
一条指令要等几十上百个周期，IPC 掉到 0.16。

`cache-references` 比 1000 万次迭代还多，是因为填充这块 256 MiB 数组本身也会产生访问。
数量级看迭代次数即可。

## 五、实验三：随机分支，猜错就清流水线

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_metrics/02_stat_metrics branch
```

实际输出：

```
branch(300000000) = 8805418046098163309
elapsed: 1.26829 s
     5,382,544,889      cpu_core/cycles/
     6,218,785,322      cpu_core/instructions/
     1,211,570,818      cpu_core/branches/
       150,062,377      cpu_core/branch-misses/
           267,185      cpu_core/cache-references/
            73,978      cpu_core/cache-misses/
```

三个比值：

- IPC = 62.2 亿 / 53.8 亿 ≈ **1.16**；
- 分支预测错误率 = 1.50 亿 / 12.1 亿 ≈ **12.4%**；
- 缓存未命中率 ≈ 28%，同样只有 26 万次访问，可忽略。

结论：每次循环有一个结果随机的 `if`，分支预测器大约猜错一半。
一次猜错就要丢掉流水线里已经取的指令，IPC 从 4 掉到 1 附近。

12.4% 之所以不是 50%，是因为 `branches` 里还包含循环判断和函数调用的返回，
这些分支很好预测，把比例稀释了。

## 六、三组结果放一起

| mode | cycles | instructions | IPC | 缓存未命中率 | 分支错误率 | 主要瓶颈 |
|---|---|---|---|---|---|---|
| `arith` | 40.4 亿 | 160.6 亿 | 3.98 | 34%（仅 25 万次） | 0.0015% | 无 |
| `cache` | 48.4 亿 | 7.5 亿 | 0.16 | 90.8% | 0.07% | 内存延迟 |
| `branch` | 53.8 亿 | 62.2 亿 | 1.16 | 28%（仅 27 万次） | 12.4% | 分支预测 |

数字每次跑都会波动，看数量和比值就够了。

## 七、本节收获

- `perf stat` 的原始计数要换算成 IPC、未命中率才有意义；
- IPC 低时，先查两类原因：缓存没命中、分支猜错；
- 比率要和绝对次数一起看，否则会把"25 万次里的 34%"当成大问题；
- 同一台机器做对比实验，绑核和固定事件名能让数字稳定下来。

第 03 课开始不再只看总数，用 `perf record` 找出这些等待具体发生在哪一行。

## 附：跑不出结果时

**1. 出现 `<not supported>` 或一整列 `<not counted>`**

多半是事件名没写 `cpu_core/` 前缀，或者进程跑到了 E 核上。
确认命令里带 `taskset -c 2`，并且六个事件都加了前缀。

**2. 括号里出现百分比**

事件数超过了硬件计数器的数量，perf 分时复用，并按比例换算了最终数字。
本课的 6 个事件通常能拿满；如果出现百分比，忽略即可，比值仍然可用。

**3. 没有 taskset**

很多发行版默认带 `util-linux`，一般都装好了。
实在没有，去掉 `taskset -c 2` 也能出数，只是比值会更飘。

**4. 想改运行时长**

第二个参数就是迭代次数，例如：

```bash
./build/lessons/02_stat_metrics/02_stat_metrics cache 3000000
```
