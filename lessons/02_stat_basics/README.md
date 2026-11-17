# 02 第一次读 `perf stat`：程序到底算了多少

第 01 课证明 perf 能跑。
这一课只回答一个问题：**`perf stat` 报出来的那几个大数字，分别在数什么？**

只用一个程序、一条命令、一个结论。

## 最短路径

先照着敲，跑通了再往下读。

```bash
# 1. 构建
cmake -S . -B build && cmake --build build -j

# 2. 先看程序自己跑多久
./build/lessons/02_stat_basics/02_stat_basics

# 3. 再看 perf 数到了什么
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_basics/02_stat_basics
```

要看的只有一个数：`instructions / cycles` 约等于 **4**。
如果对不上，先看文末附录。

## 一、示例程序

[main.cpp](main.cpp) 只有一个循环：

```cpp
std::uint64_t sum = 0;
for (std::uint64_t i = 0; i < n; ++i) {
  sum += i * 3 + 1;
}
return sum;
```

三处刻意的写法：

- 循环体只有乘加，数字全在寄存器里，不碰内存；
- 循环次数固定，`if` 每次都往同一个方向走，没有"猜错"的机会；
- 函数加 `noinline`，结果打印出来，保证编译器不会把整个循环删掉。

迭代次数默认 40 亿，实测跑 1 秒左右。
这个程序是后面几课的**对照组**：它代表"CPU 只管算、什么都不用等"能跑多快。

## 二、那条命令

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_basics/02_stat_basics
```

只有两处和第 01 课不同，都不是可以省略的细节：

| 写法 | 作用 |
|---|---|
| `taskset -c 2` | 把进程钉在 2 号 CPU 上，避免在大小核之间来回迁移 |
| `cpu_core/` 前缀 | 只数 P 核的硬件计数器，不带前缀会混进 E 核的数字 |

六个事件的含义：

| 事件 | 数的是什么 |
|---|---|
| `cycles` | 处理器时钟周期 |
| `instructions` | 执行完的指令数 |
| `branches` | 分支指令数 |
| `branch-misses` | 分支预测失败的次数 |
| `cache-references` | 访问末级缓存的次数 |
| `cache-misses` | 末级缓存没命中的次数 |

## 三、实际输出

程序自己的输出：

```
arith(4000000000) = 5553255924290448384
elapsed: 1.12534 s
```

`perf stat` 的输出：

```
 Performance counter stats for './build/lessons/02_stat_basics/02_stat_basics':

     4,033,383,585      cpu_core/cycles/
    16,063,594,393      cpu_core/instructions/
     4,010,713,903      cpu_core/branches/
            55,408      cpu_core/branch-misses/
           166,448      cpu_core/cache-references/
            39,865      cpu_core/cache-misses/

       0.80 seconds time elapsed
```

原始数字没法直接下结论，先换算成两个比值：

- **IPC** = `instructions / cycles` = 160.6 亿 / 40.3 亿 ≈ **3.98**；
- **分支预测错误率** = `branch-misses / branches` = 5.5 万 / 40.1 亿 ≈ **0.0014%**。

两段输出不是同一次运行，`perf stat` 自己也要读计数器，耗时通常会偏大一点。
比值不受影响，后面只看比值。

结论一句话：

> 每个周期执行将近 4 条指令，分支几乎没错过。
> 说明这个循环既不缺数据，也不缺指令，CPU 的流水线是满的。

这就是"算得快"的样子，记下 3.98 这个数。
下一课换一个每次都要等内存的程序，看它掉到多少。

### 为什么不是更高

4 已经接近这条循环的上限：一条乘法、一条加法、一条比较、一条跳转，
乘以流水线的发射宽度，刚好吃满。

想再高，得让代码里有更多互不依赖的指令——那是编译器的事，不是 perf 的事。

## 四、两个容易读错的地方

**比率的绝对值要配上次数看。**

本课缓存未命中率 = 3.9 万 / 16.6 万 ≈ 24%，看着不低。
但总共才 16 万次访问，相对 40 亿个周期可以忽略。
后面几课会反复遇到这种"比率高、次数少"的情况。

**数字每次跑都会变。**

同一台机器上下浮动 1%~5% 很正常。
看数量级和比值，不要盯着某一位小数。

## 五、本节收获

- `perf stat` 的原始计数要换算成比值才有意义；
- 本课的参照值：纯计算循环的 IPC ≈ 4，分支错误率 ≈ 0；
- 绑核、加 `cpu_core/` 前缀，是让数字可复现的两个前提；
- 比率必须和绝对次数一起看。

下一课把循环换成"每次都要等内存"的写法，看同一个命令的输出怎么变。

## 附：跑不出结果时

**1. 出现 `<not supported>` 或整列 `<not counted>`**

多半是事件名少了 `cpu_core/` 前缀，或者进程跑到了 E 核上。
确认命令里带 `taskset -c 2`，六个事件都加前缀。

**2. 数字后面带百分比，例如 `cpu_core/cycles/  (75.02%)`**

事件数超过了硬件计数器的数量，perf 只能分时复用再按比例换算。
本课这 6 个事件通常能一次拿满；真出现了也可以忽略，比值仍然可用。

**3. 没有 taskset**

多数发行版自带 `util-linux`。确实没有的话，去掉 `taskset -c 2` 也能出数，
只是前后两次的数字会更飘。

**4. 想改运行时长**

第一个参数就是迭代次数：

```bash
./build/lessons/02_stat_basics/02_stat_basics 1000000000
```

改完记得把每次对比都用同一个次数。
