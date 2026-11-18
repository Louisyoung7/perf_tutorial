# 03 程序在等内存：IPC 掉到 0.14

上一课的纯计算循环，IPC 是 3.98。
这一课只回答一个问题：**如果程序每一步都要等内存，`perf stat` 会变成什么样？**

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 运行，看耗时
./build/lessons/03_memory_wait/03_memory_wait

# 3. 测量
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/03_memory_wait/03_memory_wait
```

要看的只有一个数：IPC 从 3.98 掉到 **0.1 多一点**。
对不上先看附录。

## 一、示例程序

> 不关心程序的具体逻辑没关系，可以直接跳到 [二、那条命令](#二那条命令)。

[main.cpp](main.cpp) 的核心只有一个循环：

```cpp
for (std::uint64_t i = 0; i < n; ++i) {
  idx = data[idx];  // 下一次读哪里，取决于这次读到了什么
  sum += idx;
}
```

这个写法叫**指针追逐**。两个关键点：

- 下一次的地址是这次读到的值，CPU 没法提前发请求；
- 数组 256 MiB，远大于测试环境 36 MiB 的 L3，下一站大概率不在缓存里。

于是每次迭代都是"读内存 → 拿到地址 → 再读内存"，一条串行链。

### 那条链是怎么造出来的

准备阶段只做一件事：把数组填成一张随机"下一站"表。

```cpp
const std::uint64_t mask = data.size() - 1;
for (std::size_t i = 0; i < data.size(); ++i) {
  data[i] = (i * 1103515245ULL + 12345ULL) & mask;
}
```

`i * a + c` 再截断，在 `a`、`c` 都取奇数、模数是 2 的幂时是一个**置换**：
每个下标恰好出现一次，从 0 出发会走遍全部 32M 个位置，不会提前成环。

这三行不需要看懂细节，记住结论就够：
**填完之后，`data` 就是一条随机打乱、长度 32M 的链表。**

这一步在计时之外，不计入本课的热点。

## 二、那条命令

和上一课完全一样，只换了程序路径：

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/03_memory_wait/03_memory_wait
```

命令不换，是为了让两次实验只有一个变量：程序访问内存的方式。

## 三、实际输出

程序自己的输出：

```
chase(10000000) = 167759082871360
elapsed: 0.909796 s
```

一次迭代约 91 纳秒——远远超过一条加法需要的零点几纳秒。

`perf stat` 的输出：

```
 Performance counter stats for './build/lessons/03_memory_wait/03_memory_wait':

     5,283,664,704      cpu_core/cycles/
       751,911,164      cpu_core/instructions/
       124,176,830      cpu_core/branches/
            88,893      cpu_core/branch-misses/
        21,470,859      cpu_core/cache-references/
        19,078,219      cpu_core/cache-misses/

       1.37 seconds time elapsed
```

三个比值：

| 指标     | 上一课（纯计算）  | 本课（指针追逐）  |
| ------ | ------------ | --------- |
| IPC    | 3.98         | **0.14**  |
| 缓存未命中率 | 24%（仅 16 万次） | **88.8%** |
| 分支错误率  | 0.0014%      | 0.07%     |

结论一句话：

> IPC 从 4 掉到 0.14，缓存未命中率 88.8%，分支几乎没错。
> 时间花在等内存上。

两段输出来自两次运行：`perf stat` 要同时读 6 个计数器，耗时会被拉长。
比值不受影响。

### 数字怎么看

- 52.8 亿个周期里只执行了 7.5 亿条指令，平均一条指令要等 7 个周期；
- 每 100 次末级缓存访问，有 88 次要去内存拿；
- `cache-references` 有 2147 万，比 1000 万次迭代还多，
  因为填充那 256 MiB 数组本身也要访问内存，看数量级即可；
- 分支错误率只有 0.07%，排除掉"分支猜错"这个嫌疑。

所以瓶颈只剩一个：内存延迟。

## 四、这件事说明了什么

同一台机器、同一条命令，只是把"访问寄存器"换成"访问一个比缓存大得多的数组"，
IPC 就差了两个数量级。

这就是 `perf stat` 的用法：

1. 先看 IPC，判断程序是"算得慢"还是"在等"；
2. 再看不命中率和分支错误率，看它在等什么；
3. 最后用 `perf record` 找具体位置——之后几课开始做这件事。

## 五、本节收获

- IPC 低不代表代码写得差，可能只是在等内存；
- 指针追逐是制造"必须等内存"的最短写法：地址依赖 + 数组远大于缓存；
- 缓存未命中率要配上 `cache-references` 的绝对次数看；
- 命令不变、只换程序，才能说结论是程序差异带来的。

接下来换一种"等"：分支猜错。

## 附：跑不出结果时

**1. 程序跑得很快，只有零点几秒**

数组可能是全零，或者内存没真正写进去。
全零数组的阅读会落在同一个共享零页上，全程命中缓存，10M 次只要几十毫秒。
本课用 `(i * a + c) & mask` 填满真实数据，就是为了避免这种情况。

**2. IPC 没有掉到 0.1 附近**

先确认程序真的跑满 1000 万次迭代（看它打印的 `chase(...)` 行）。
如果机器 L3 特别大（远大于 256 MiB），未命中率会下降，IPC 会高一些。
遇到这种情况把数组扩大：

```cpp
std::vector<std::uint64_t> data(64ULL * 1024 * 1024);  // 512 MiB
```

注意 `mask = data.size() - 1` 对 2 的幂成立，扩容时保持 2 的幂。

**3. 出现 `<not supported>`**

和纯计算那一课附录第 1 条相同：检查 `taskset -c 2` 和 `cpu_core/` 前缀。

**4. 想改运行时长**

第一个参数是迭代次数：

```bash
./build/lessons/03_memory_wait/03_memory_wait 3000000
```
