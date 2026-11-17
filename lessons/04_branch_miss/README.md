# 04 分支猜错：一次失误要丢掉多少

第 03 课看到 IPC 掉到 0.14，原因是等内存。
这一课只回答一个问题：**如果瓶颈不是内存，而是"猜错了下一步走哪"，`perf stat` 又是什么样？**

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 运行
./build/lessons/04_branch_miss/04_branch_miss

# 3. 测量
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/04_branch_miss/04_branch_miss
```

要看的只有一个数：分支错误率 **10% 以上**，而 IPC 在 1 附近。
对不上先看附录。

## 一、示例程序

[main.cpp](main.cpp) 的核心：

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

前三行是 xorshift，一种很短的伪随机数生成器：三步移位、异或。
它几乎不花时间，唯一作用是让 `x` 的最低位看起来完全随机。

`if (x & 1)` 按这个随机位选边，下一次走哪边和历史无关。
分支预测器再厉害，也只能猜中一半左右。

### 为什么两边要写成函数调用

如果两个分支都只写一条加减，编译器会把 `if` 改写成 `cmov` 这类**无分支指令**，
条件跳转从二进制里消失，实验就做不成了。

两个分支各调一个 `noinline` 的小函数，条件跳转才会真的留下。

迭代次数默认 3 亿，实测 1 秒左右。

## 二、那条命令

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/04_branch_miss/04_branch_miss
```

和第 02、03 课完全一致。三课共用一条命令，才好横向比较。

## 三、实际输出

程序自己的输出：

```
branch(300000000) = 8805418046098163309
elapsed: 1.11648 s
```

`perf stat` 的输出：

```
 Performance counter stats for './build/lessons/04_branch_miss/04_branch_miss':

     5,384,056,197      cpu_core/cycles/
     6,217,317,921      cpu_core/instructions/
     1,211,298,958      cpu_core/branches/
       150,052,261      cpu_core/branch-misses/
           279,795      cpu_core/cache-references/
            71,288      cpu_core/cache-misses/

       1.50 seconds time elapsed
```

三个比值：

| 指标 | 第 02 课（纯计算） | 第 03 课（等内存） | 本课（随机分支） |
|---|---|---|---|
| IPC | 3.98 | 0.14 | **1.15** |
| 缓存未命中率 | 24%（16 万次） | 88.8% | 25%（仅 28 万次） |
| 分支错误率 | 0.0014% | 0.07% | **12.4%** |

结论一句话：

> 内存几乎不碰（28 万次访问可以忽略），分支错了 1.5 亿次，
> IPC 从 4 掉到 1.15。

两段输出不是同一次运行，`perf stat` 的 elapsed 会偏大，比值不受影响。

### 为什么错误率不是 50%

每条分支里有三种：

- 循环自己的判断和跳转——每次都一样，几乎全对；
- 两个 `on_odd` / `on_even` 的函数返回——也很好预测；
- 那个 `if (x & 1)`——接近一半会错。

前两种把比例稀释了，所以总错误率停在 12.4%。
如果只看那个 `if`，错误率接近 50%。

### 猜错的代价

- 53.8 亿个周期执行了 62.2 亿条指令，平均一条指令摊到 0.87 个周期；
- 1.5 亿次猜错，每次都要丢掉已经取进流水线的指令，重新排队；
- 这个代价比"等内存"小，所以 IPC 停在 1.15，而不是 0.14。

三课对比下来，可以形成一个很实用的判断：

| IPC | 常见原因 | 下一步查什么 |
|---|---|---|
| 3~4 | 没有明显瓶颈 | 不用优化 |
| 1 左右 | 分支预测失败、依赖链太长 | 分支错误率 |
| 远低于 1 | 等内存、等锁、等 IO | 缓存未命中率 |

## 四、本节收获

- 分支错误率要和 `branches` 一起看，循环分支会稀释比例；
- 想测分支，先保证条件跳转真的存在，别被编译器改成无分支指令；
- IPC 是三个指标里最好用的入口：先定位"在等"还是"在想"，再往下查；
- 三课共用同一条命令，比值才有可比性。

到这里，`perf stat` 的用法就齐了：它只能告诉你"哪一类问题"，不告诉你"哪一行"。
下一课换工具，把时间定位到具体的代码行。

## 附：跑不出结果时

**1. 分支错误率只有零点几个百分点**

编译器的 `if` 被改写成了无分支指令。检查两个分支是否各调了一个 `noinline` 函数。
本课之所以绕这一圈，就是为了挡住这种改写。

**2. 程序跑得非常快**

迭代次数被改小了。默认 3 亿次约 1 秒：

```bash
./build/lessons/04_branch_miss/04_branch_miss
```

**3. 缓存未命中率也很高**

看 `cache-references` 的绝对次数。本课只有 28 万次，
相对 12 亿条分支指令可以忽略。比率高、次数少，就不是主要矛盾。

**4. 出现 `<not supported>`**

和第 02 课附录第 1 条相同：检查 `taskset -c 2` 和 `cpu_core/` 前缀。

**5. 想改运行时长**

第一个参数是迭代次数：

```bash
./build/lessons/04_branch_miss/04_branch_miss 100000000
```
