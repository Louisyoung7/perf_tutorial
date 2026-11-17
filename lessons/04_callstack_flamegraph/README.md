# 04 调用栈与火焰图

第 03 课知道了热点在 `mix` 的某一行。
这一课回答下一个问题：**这个热点是被谁调用的？调用链有多深？**

工具还是 `perf record`，区别是要把调用栈完整采下来，
再按调用关系汇总成树，最后铺开成火焰图。

## 一、示例程序

源码：[main.cpp](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/04_callstack_flamegraph/main.cpp)

```bash
./build/lessons/04_callstack_flamegraph/04_callstack_flamegraph [倍率]
```

程序只有一个热点 `mix`，但有三条入口路径：

| 入口 | 调用链 | 迭代次数 |
|---|---|---|
| `handle_query` | `handle_query` → `parse_header` → `decode_request` → `mix` | 500 万 |
| `handle_render` | `handle_render` → `blend_layers` → `mix` | 300 万 |
| `handle_index` | `handle_index` → `walk_tree`（递归 8 层）→ `mix` | 每层 25 万 |

`mix` 在第 03 课已经用过：在 256 MiB 的表里追指针，每次访问都要等内存。
三条链路的迭代次数比例是 `5 : 3 : 2.25`，下面用它来对照报告的占比。

### 1. 为什么要把一个热点拆成三条链

如果只写 `main → mix`，调用栈只有一个分支，看不出"谁在调用热点"。
拆成三条链后，同样一个 `mix` 会被三个不同的父函数调用，
扁平报告里只有一行 `mix`，按调用关系展开后才会分成三份。

`walk_tree` 的递归用来制造深度。
递归每深一层，火焰图里就多一层同名方块。

### 2. 两个必须注意的写法

**不能写成尾调用。**

如果按最自然的方式写：

```cpp
std::uint64_t decode_request(std::uint64_t seed) {
  return mix(seed, 5'000'000);
}
```

报告里 `decode_request`、`parse_header`、`handle_query` 全都不见了，
`mix` 的调用者直接变成 `main`。
原因是编译器把 `return f(...)` 优化成尾调用，中间函数的栈帧不再保留。

正确写法是拿到结果后再做一次运算：

```cpp
std::uint64_t decode_request(std::uint64_t seed) {
  const std::uint64_t r = mix(seed, 5'000'000 * g_scale);
  return r ^ seed;
}
```

中间的栈帧才留了下来。

**入口参数不能是编译期常量。**

入口种子取自运行时变量 `g_scale`：

```cpp
const std::uint64_t seed = 0x1234 ^ lesson04::g_scale;
```

如果直接写常量，编译器会按常量把整条调用链特化成 `[clone .constprop.0]` 副本，
报告里的函数名会多出一串后缀，火焰图也更难读。

### 3. 运行

```bash
./build/lessons/04_callstack_flamegraph/04_callstack_flamegraph
```

实际输出：

```
query  = 11400640294658986316
render = 50347520033378
index  = 4204220229989
elapsed: 0.977209 s
```

数组填充和计时方式沿用前两课，这里不再重复解释。

## 二、采样：把调用栈记下来

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/04_callstack_flamegraph/04_callstack_flamegraph
```

和第 03 课相比，只把 `-g` 换成了 `--call-graph dwarf`。

- `-g` 默认按**帧指针**展开调用栈。
- `--call-graph dwarf` 按**调试信息**展开调用栈。

本课的 `mix` 是叶子函数，编译器没有为它建立帧指针。
按帧指针展开时，perf 会跳过直接调用 `mix` 的那一层，
报告里 `decode_request` 和 `blend_layers` 会消失。
代价是数据变大了，附录第 2 条有对照。

实际输出：

```
[ perf record: Woken up 35 times to write data ]
[ perf record: Captured and wrote 8.537 MB perf.data (1085 samples) ]
```

样本数和第 03 课差不多，但文件从 0.1 MB 涨到 8.5 MB，多出来的就是栈数据。

## 三、先看汇总：热点还是它

```bash
perf report --stdio -i perf.data -g none --no-children --percent-limit 0.5
```

`-g none` 表示这次不展开调用树，只看函数汇总；`--no-children` 只看函数自身的开销。

实际输出：

```
# Overhead  Command          Shared Object            Symbol
# ........  ...............  .......................  ...............................................
#
    90.23%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::mix(unsigned long, unsigned long)
     2.18%  04_callstack_fl  libc.so.6                [.] __memset_avx2_unaligned_erms
     1.54%  04_callstack_fl  04_callstack_flamegraph  [.] main
     0.53%  04_callstack_fl  [unknown]                [k] 0xffffffff97601268
```

结论和上一课一样：`mix` 占 90.23%，还是唯一的热点。
`memset` 是准备 256 MiB 数组时清零页面，`main` 是填充和计时。
内核地址解析不出来，原因和上一课附录相同。

扁平报告只能说明"时间在 `mix` 里"，说明不了"时间从哪条路来"。
下一节换个排序方式。

## 四、看调用者：谁把时间花在了 mix 上

```bash
perf report --stdio -i perf.data --children -g none --no-inline --percent-limit 3
```

`--children` 把统计口径换成**包含子调用**，输出多一列 `Children`。

实际输出（只留程序自身的函数）：

```
# Children      Self  Command          Shared Object            Symbol
# ........  ........  ...............  .......................  ...................................
#
    90.50%    90.23%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::mix(unsigned long, unsigned long)
    44.16%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::decode_request(unsigned long)
    44.16%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::handle_query(unsigned long)
    44.16%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::parse_header(unsigned long)
    26.30%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::blend_layers(unsigned long)
    26.30%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::handle_render(unsigned long)
    20.03%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::handle_index(unsigned long)
    20.03%     0.00%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::walk_tree(unsigned long, int)
```

两个列的区别：

| 列 | 含义 | 用来看什么 |
|---|---|---|
| `Self` | 只算函数自身指令的样本 | 热点在哪 |
| `Children` | 自身 + 所有子调用的样本 | 这条路径总共占多少 |

读这张表：

- `mix` 的 `Self` 是 90.23%，`Children` 是 90.50%，几乎相等。
  说明时间基本都花在 `mix` 内部，外层函数只是传参、等结果。
- `decode_request`、`parse_header`、`handle_query` 都是 44.16%。
  它们在同一条链上，每一层都继承了 `mix` 的全部时间，所以数字相同。
  `Self` 为 0 不代表它们不执行，只代表自身指令少到采不到样本。
- 三条链的占比是 `44.16% : 26.30% : 20.03%`。
  和迭代次数 `5 : 3 : 2.25` 的比值对得上（1.68、2.20 对 1.67、2.22）。

这就是本课要的结论：
**热点只有一个，但它被三条路径分摊，比例由调用方决定。**

## 五、看完整调用树

去掉 `--children`，恢复默认的调用树展开：

```bash
perf report --stdio -i perf.data --no-children --percent-limit 2 --no-inline
```

实际输出：

```
# Overhead  Command          Shared Object            Symbol                                         
# ........  ...............  .......................  ...............................................
#
    90.38%  04_callstack_fl  04_callstack_flamegraph  [.] lesson04::mix(unsigned long, unsigned long)
            |
            ---lesson04::mix(unsigned long, unsigned long)
               |          
               |--43.81%--lesson04::decode_request(unsigned long)
               |          lesson04::parse_header(unsigned long)
               |          lesson04::handle_query(unsigned long)
               |          main
               |          __libc_start_call_main
               |          __libc_start_main@@GLIBC_2.34
               |          _start
               |          
               |--26.69%--lesson04::blend_layers(unsigned long)
               |          lesson04::handle_render(unsigned long)
               |          main
               |          __libc_start_call_main
               |          __libc_start_main@@GLIBC_2.34
               |          _start
               |          
                --19.88%--lesson04::walk_tree(unsigned long, int)
                          |          
                          |--17.58%--lesson04::walk_tree(unsigned long, int)
                          |          |          
                          |          |--15.38%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          
                          |          |          |--13.17%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          |          
                          |          |          |          |--11.05%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          |          |          
                          |          |          |          |          |--8.94%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          |          |          |          
                          |          |          |          |          |          |--6.72%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          |          |          |          |          
                          |          |          |          |          |          |          |--4.51%--lesson04::walk_tree(unsigned long, int)
                          |          |          |          |          |          |          |          lesson04::handle_index(unsigned long)
                          |          |          |          |          |          |          |          main
                          |          |          |          |          |          |          |          __libc_start_call_main
                          |          |          |          |          |          |          |          __libc_start_main@@GLIBC_2.34
                          |          |          |          |          |          |          |          _start
                          |          |          |          |          |          |          |          
                          |          |          |          |          |          |           --2.22%--lesson04::handle_index(unsigned long)
                          |          |          |          |          |          |                     main
                          |          |          |          |          |          |                     __libc_start_call_main
                          |          |          |          |          |          |                     __libc_start_main@@GLIBC_2.34
                          |          |          |          |          |          |                     _start
                          |          |          |          |          |          |          
                          |          |          |          |          |           --2.21%--lesson04::handle_index(unsigned long)
                          |          |          |          |          |                     main
                          |          |          |          |          |                     __libc_start_call_main
                          |          |          |          |          |                     __libc_start_main@@GLIBC_2.34
                          |          |          |          |          |                     _start
                          |          |          |          |          |          
                          |          |          |          |           --2.11%--lesson04::handle_index(unsigned long)
                          |          |          |          |                     main
                          |          |          |          |                     __libc_start_call_main
                          |          |          |          |                     __libc_start_main@@GLIBC_2.34
                          |          |          |          |                     _start
                          |          |          |          |          
                          |          |          |           --2.13%--lesson04::handle_index(unsigned long)
                          |          |          |                     main
                          |          |          |                     __libc_start_call_main
                          |          |          |                     __libc_start_main@@GLIBC_2.34
                          |          |          |                     _start
                          |          |          |          
                          |          |           --2.21%--lesson04::handle_index(unsigned long)
                          |          |                     main
                          |          |                     __libc_start_call_main
                          |          |                     __libc_start_main@@GLIBC_2.34
                          |          |                     _start
                          |          |          
                          |           --2.20%--lesson04::handle_index(unsigned long)
                          |                     main
                          |                     __libc_start_call_main
                          |                     __libc_start_main@@GLIBC_2.34
                          |                     _start
                          |          
                           --2.30%--lesson04::handle_index(unsigned long)
                                     main
                                     __libc_start_call_main
                                     __libc_start_main@@GLIBC_2.34
                                     _start

     2.14%  04_callstack_fl  libc.so.6                [.] __memset_avx2_unaligned_erms
            |
            ---0xffffffff9e200b90
```

这棵树从 `mix` 往上读：每一行是上一行的调用者，缩进代表栈的深度。

先注意排版约定：百分比只印在分支的第一行。
例如 `|--43.81%--decode_request` 下面是 `parse_header`、`handle_query`、`main`，
它们共用这个 43.81%，不是各自再占一份。

**三条链的根**

- 第一条 43.81%：`decode_request` → `parse_header` → `handle_query` → `main`。
- 第二条 26.69%：`blend_layers` → `handle_render` → `main`。
- 第三条 19.88%：`walk_tree` 递归，最后回到 `handle_index` → `main`。
- 再加 `memset` 的 2.14%、启动库和其他内核样本，合计 100%。

**递归那段**

- 百分比逐层变小：19.88 → 17.58 → 15.38 → 13.17 → 11.05 → 8.94 → 6.72 → 4.51。
- 每一层的含义是"栈里至少有这么多层 `walk_tree` 的样本"，
  不是"这一层自己花了这么多时间"。
- 最外层 19.88% 表示所有进入过递归的样本；
  最深一层 4.51% 表示栈里已经叠了 8 层 `walk_tree`。
  越往下层越窄，火焰图里就会呈现阶梯状。

**每个递归层下面的 handle_index 分支**

- 递归一共有 9 个 `mix` 调用点：depth 0 一次，depth 1～8 返回后各一次。
- 那几个约 2.2% 的分支，是 `walk_tree` 从子递归返回后自己调用的那次 `mix`，
  对应代码里的 `child ^ mix(...)`。
- 每层调用 25 万次，单次约占 2.2%，七个分支合计约 15.4%。
- 最深的一支是 4.51%，正好是其他分支的两倍。
  因为 depth 0 的 `return mix(...)` 也被编译器做成了尾调用，
  它和 depth 1 返回后的 `mix` 落在同一个栈深度上，两个调用点的样本并到了一起。

**启动帧**

`main` 后面的 `__libc_start_call_main`、`__libc_start_main@@GLIBC_2.34`、`_start`
是进程启动代码。它们出现在每条链的末尾，只是同一条栈的延续，不额外占时间。
`__memset_avx2_unaligned_erms` 下面是内核地址，是符号解析不出来，不是本程序的热点。

这棵树的数字和上一节 `--children` 表来自两次采样，差 1% 左右是正常的。

## 六、生成火焰图

调用树已经能回答"谁调用了热点"，但它不适合分享，也不容易一眼看出比例。
火焰图把同一棵树按样本宽度横向铺开。

生成需要 Brendan Gregg 的两个脚本：

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph

perf script -i perf.data --no-inline \
  | /tmp/FlameGraph/stackcollapse-perf.pl > perf.folded
/tmp/FlameGraph/flamegraph.pl perf.folded > flamegraph.svg
```

三个文件的作用：

| 文件 | 内容 |
|---|---|
| `perf.data` | 二进制采样数据 |
| `perf.folded` | 每行一条调用栈，行尾是样本数 |
| `flamegraph.svg` | 浏览器直接打开的矢量图 |

`perf.folded` 的实际内容（省略启动帧）：

```
main;lesson04::handle_query;lesson04::parse_header;lesson04::decode_request;lesson04::mix 2126740920
main;lesson04::handle_render;lesson04::blend_layers;lesson04::mix 1253533851
main;lesson04::handle_index;lesson04::walk_tree;...;lesson04::mix 210191445
```

一行就是一条唯一的调用栈，分号分隔，最后是这条栈的样本权重。
相同的栈会被合并，所以行数远少于样本数。

本仓库已经生成了一份：[flamegraph.svg](file:///home/louis/Documents/VSCodeProjects/CPlusPlusProjects/perf_tutorial/lessons/04_callstack_flamegraph/flamegraph.svg)

看图方法：

- 横轴宽度是样本占比，等价于时间占比；纵轴是调用深度。
- 底部是 `_start`，往上依次是 `main`、三条入口、`mix`。
- 三个分支的宽度大约是 44%、26%、20%，和 `--children` 表一致。
- `walk_tree` 是阶梯状：每往上一层更窄，因为只有更深的样本才经过全部递归帧。
- 颜色只是区分相邻方块，没有性能含义。

## 七、本节收获

- `--call-graph dwarf` 采到完整调用栈；`-g` 默认用帧指针，会漏掉叶子函数的上一层；
- `Self` 回答"热点在哪"，`Children` 回答"这条路径总共占多少"；
- 同一条链上的函数 `Children` 相同，因为每一层都包含下面全部时间；
- 火焰图就是调用树按样本数铺开，横轴是时间，纵轴是深度，宽度看比例；
- 想让栈可读，函数不能内联、不能尾调用，入口参数不要给编译器留下常量特化的机会。

下一课处理另一种常见情况：报告里只剩地址，看不到函数名和行号。

## 附：跑不出结果时

**1. 调用链里只有 `main`，中间函数全消失**

编译器把 `return f(...)` 优化成了尾调用，中间栈帧不再存在。
在函数里拿到结果后再做一步运算，例如 `return r ^ seed;`。
也可以给整个工程加 `-fno-optimize-sibling-calls`，但会改变其他课的代码生成，不推荐。

**2. `decode_request`、`blend_layers` 不见了**

`-g` 默认按帧指针展开。
`mix` 是叶子函数，没有自己的帧指针，perf 会跳过调用它的那一层。
同一次运行的 `-g` 报告里，`Self` 列大致是这样：

```
    43.98%  lesson04::handle_query
    43.98%  lesson04::parse_header
    26.96%  lesson04::handle_render
    19.89%  lesson04::handle_index
    17.62%  lesson04::walk_tree
```

直接调用 `mix` 的 `decode_request`、`blend_layers` 都不在。
换成 `--call-graph dwarf` 即可，本课正文用的就是它。

**3. 火焰图里出现 `[unknown]` 或函数名缺失**

`stackcollapse-perf.pl` 会把以 `(` 开头的符号当成进程名跳过。
匿名命名空间里的函数，符号正是 `(anonymous namespace)::foo` 开头。
改成具名命名空间，或者放到全局作用域。

**4. 提示找不到 `stackcollapse-perf.pl`**

脚本没有随 perf 安装，需要单独下载：

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
```

两个脚本都依赖 Perl，测试环境自带。

**5. 火焰图底部一大片 `[unknown]` 或 `0xffffffff...`**

这些是内核样本。
`/proc/kallsyms` 受权限限制，perf 解析不出内核符号。
本课它们只占 1% 左右，忽略即可。

**6. 想改运行时长**

参数是倍率，三条链的迭代次数一起放大：

```bash
./build/lessons/04_callstack_flamegraph/04_callstack_flamegraph 2
```

`dwarf` 展开会产生更多数据，倍率调大时注意 `perf.data` 的体积。
