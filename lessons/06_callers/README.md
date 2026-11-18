# 06 热点是被谁调用的

上一课把时间定位到了 `main.cpp:27`。
这一课只回答一个问题：**这行代码是被谁调用的？两条路径各占多少？**

工具还是 `perf record`，只多一个参数：把调用栈完整采下来。

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 采样（约 1 秒）
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/06_callers/06_callers

# 3. 看调用者
perf report --stdio -i perf.data --children -g none --no-inline
```

要看的只有一处：报告多出一列 `Children`，热点被人分成了两份。
对不上先看附录。

## 一、示例程序

> 不关心程序的具体逻辑没关系，可以直接跳到 [二、那条命令](#二那条命令)。

[main.cpp](main.cpp) 只有一个热点 `mix`，但有两个入口：

| 入口 | 调用链 | 迭代次数 |
|---|---|---|
| `handle` | `handle` → `decode` → `mix` | 600 万 |
| `render` | `render` → `mix` | 400 万 |

`mix` 就是等内存那一课的指针追逐，在 256 MiB 的表里随机跳。
两条链的迭代次数是 `6 : 4`，下一节拿它对照报告里的占比。

### 为什么要拆成两条链

如果只写 `main → mix`，调用栈只有一个分支，看不出"谁在调用热点"。
拆成两条长度不同的链之后：

- 扁平报告里只有一行 `mix`；
- 按调用关系展开后，同一个 `mix` 会被分成两份，比例由两个入口决定。

### 一处必须注意的写法

```cpp
__attribute__((noinline)) std::uint64_t decode(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 6'000'000 * g_scale);
  return r ^ seed;   // 拿到结果再做一步运算
}
```

如果直接写 `return mix(...)`，编译器会把它优化成**尾调用**，
`decode` 的栈帧不再保留，报告里这一层直接消失。

代价只是多一条异或，值得。

## 二、那条命令

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/06_callers/06_callers
```

和上一课只差一个参数：`-g` 换成 `--call-graph dwarf`。

| 参数 | 展开方式 |
|---|---|
| `-g` | 按帧指针展开调用栈 |
| `--call-graph dwarf` | 按调试信息展开调用栈 |

本课的 `mix` 是叶子函数，编译器没给它建帧指针。
按帧指针展开时，perf 会跳过直接调用 `mix` 的那一层，
`decode`、`render` 都会消失。

实际输出：

```
handle = 100634835994411
render = 67091302756345
elapsed: 0.935268 s
[ perf record: Woken up 33 times to write data ]
[ perf record: Captured and wrote 8.205 MB perf.data (1036 samples) ]
```

样本数和上一课差不多，文件却从 0.1 MB 涨到 8.2 MB——多出来的就是栈数据。

## 三、先确认热点没变

```bash
perf report --stdio -i perf.data -g none --no-children
```

`-g none` 表示这次不展开调用树，只看函数汇总：

```
# Overhead  Command     Shared Object         Symbol
# ........  ..........  ....................  .................................
#
    90.71%  06_callers  06_callers            [.] lesson06::mix(...)
     1.76%  06_callers  06_callers            [.] main
     1.61%  06_callers  libc.so.6             [.] __memset_avx2_unaligned_erms
```

和上一课一样：热点只有一个，占九成。
扁平报告只能说明"时间在 `mix` 里"，说不出"从哪条路来"。

## 四、换一种口径：看 Children

```bash
perf report --stdio -i perf.data --children -g none --no-inline
```

`--children` 把统计口径换成**包含子调用**，输出多一列 `Children`。
`--no-inline` 让被内联展开的代码不再单独成行，同一个函数不会被拆成好几行。

实际输出（只留程序自身的函数）：

```
# Children      Self  Command     Shared Object         Symbol
# ........  ........  ..........  ....................  .................................
#
    90.81%    90.71%  06_callers  06_callers            [.] lesson06::mix(...)
    54.61%     0.00%  06_callers  06_callers            [.] lesson06::decode(...)
    54.61%     0.00%  06_callers  06_callers            [.] lesson06::handle(...)
    36.20%     0.00%  06_callers  06_callers            [.] lesson06::render(...)
     4.88%     0.00%  06_callers  06_callers            [.] std::vector<...>::_M_default_append(...)
```

两列的区别：

| 列 | 含义 | 回答什么 |
|---|---|---|
| `Self` | 只算函数自身指令的样本 | 热点在哪 |
| `Children` | 自身 + 所有子调用的样本 | 这条路径总共占多少 |

读这张表：

- `mix` 的 `Self` 90.71%，`Children` 90.81%，几乎相等。
  说明时间都花在 `mix` 内部，外层函数只是传参、等结果。
- `decode` 和 `handle` 都是 54.61%，因为它们在同一条链上，
  每一层都继承了 `mix` 的全部时间。
  `Self` 为 0 不代表没执行，只代表自身指令少到采不到样本。
- 两条链的占比是 `54.61% : 36.20%`，比值 1.51，
  和迭代次数 `6 : 4` 对得上。

这就是本课要的结论：

> **热点只有一个，但它被两条路径分摊，比例由调用方决定。**

## 五、本节收获

- `--call-graph dwarf` 采到完整调用栈，`-g` 只按帧指针展开，会漏掉叶子函数的上一层；
- `Self` 回答"热点在哪"，`Children` 回答"这条路径总共占多少"；
- 同一条链上的函数 `Children` 相同，因为每层都包含下面全部时间；
- 想让栈可读：函数不能内联、不能尾调用，入口参数不要给编译器常量特化的机会；
- 采样文件变大是正常代价，栈数据比样本本身大得多。

到这里能回答"谁调用了热点"，但树状文本仍然不好看比例、也不好分享。
接下来把它画成图。

## 附：跑不出结果时

**1. 调用链里只有 `main`，中间函数全消失**

编译器把 `return f(...)` 优化成尾调用，中间栈帧不再存在。
在函数里拿到结果后再做一步运算，例如 `return r ^ seed;`。

**2. `decode`、`render` 不见了**

`-g` 默认按帧指针展开，而 `mix` 是叶子函数，没有自己的帧指针。
换成 `--call-graph dwarf` 即可，本课正文用的就是它。

**3. 想看树状结构**

去掉 `-g none`（用回默认的 `-g graph`），`perf report` 才会按调用树展开：

```bash
perf report --stdio -i perf.data --no-inline --percent-limit 3
```

实际输出（截取主干）：

```
    98.35%     0.00%  [.] _start
            |
            ---_start
               __libc_start_main@@GLIBC_2.34
               __libc_start_call_main
                --97.45%--main
                          |--54.61%--lesson06::handle(unsigned long)
                          |          lesson06::decode(unsigned long)
                          |          lesson06::mix(unsigned long, unsigned long)
                          |--36.20%--lesson06::render(unsigned long)
                                     lesson06::mix(unsigned long, unsigned long)
```

从下往上读：每一行是上一行的调用者，缩进代表栈的深度。
百分比只印在分支的第一行，下面几行共用这个数。

**4. 报告里出现 `0xffffffff...`**

内核样本，`/proc/kallsyms` 受权限限制解析不出符号。
本课约占 4%，忽略即可。

**5. 想改运行时长**

参数是倍率，两条链一起放大：

```bash
./build/lessons/06_callers/06_callers 2
```

倍率调大时注意 `perf.data` 的体积，dwarf 展开的数据量增长很快。

**6. 换成 `-g` 会看到什么**

用 `-g` 再采一次，`Children` 列只剩这些：

```
    90.75%    90.47%  [.] lesson06::mix(unsigned long, unsigned long)
    54.58%     0.00%  [.] lesson06::handle(unsigned long)
```

`decode` 和 `render` 直接消失，两条链也分不开了。
原因是 `mix` 没有帧指针，perf 无法把样本接回到直接调用它的那一层，
只能一路退到还有帧指针的 `handle` 上。
