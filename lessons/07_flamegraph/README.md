# 07 把调用栈画成火焰图

第 06 课用 `--children` 和调用树看清了"谁调用了热点"，但文本不容易分享，也不容易一眼看出比例。
这一课只做一件事：**把同一份采样画成火焰图。**

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 采样（约 1 秒，和第 06 课用的是同一个程序）
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/07_flamegraph/07_flamegraph

# 3. 转成折叠栈
perf script -i perf.data --no-inline \
  | /tmp/FlameGraph/stackcollapse-perf.pl > perf.folded

# 4. 画图
/tmp/FlameGraph/flamegraph.pl perf.folded > flamegraph.svg
```

用浏览器打开 `flamegraph.svg` 就能看。
脚本还没准备好，先看第二节。

## 一、程序与采样

本课不另写示例程序，直接复用第 06 课的源码，保证两次采样对象完全一致：

```cmake
add_executable(07_flamegraph ${CMAKE_CURRENT_SOURCE_DIR}/../06_callers/main.cpp)
```

程序结构还是那两条链：

| 入口 | 调用链 | 迭代次数 |
|---|---|---|
| `handle` | `handle` → `decode` → `mix` | 600 万 |
| `render` | `render` → `mix` | 400 万 |

采样命令也和第 06 课一致，`--call-graph dwarf` 是必须的：

```
handle = 100634835994411
render = 67091302756345
elapsed: 0.935268 s
[ perf record: Woken up 33 times to write data ]
[ perf record: Captured and wrote 8.205 MB perf.data (1036 samples) ]
```

## 二、准备两个脚本

火焰图不是 perf 自带的功能，需要 Brendan Gregg 的两个 Perl 脚本：

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
```

| 脚本 | 作用 |
|---|---|
| `stackcollapse-perf.pl` | 把 `perf script` 的逐条栈压成"每行一条栈 + 样本数" |
| `flamegraph.pl` | 把折叠栈画成 SVG |

两个脚本都依赖 Perl，测试环境自带。

## 三、两步生成

```bash
perf script -i perf.data --no-inline \
  | /tmp/FlameGraph/stackcollapse-perf.pl > perf.folded

/tmp/FlameGraph/flamegraph.pl perf.folded > flamegraph.svg
```

三个文件的关系：

| 文件 | 内容 |
|---|---|
| `perf.data` | 二进制采样数据 |
| `perf.folded` | 纯文本，每行一条调用栈，行尾是样本数 |
| `flamegraph.svg` | 浏览器直接打开的矢量图 |

`--no-inline` 的作用是把内联的函数单独折成一层。
不加也能画，但同一个函数会因为内联出现在好几处，看起来更乱。

实际生成了 26 行折叠栈。含热点的两行是：

```
06_callers;_start;__libc_start_main@@GLIBC_2.34;__libc_start_call_main;main;lesson06::handle;lesson06::decode;lesson06::mix 2867630312
06_callers;_start;__libc_start_main@@GLIBC_2.34;__libc_start_call_main;main;lesson06::render;lesson06::mix 1895625157
```

读法：

- 一行就是一条唯一的调用栈，分号分隔，从进程名开始，最深的是行尾；
- 行末的数字是这条栈累计的样本权重；
- 相同的栈会被合并，所以行数远少于样本数（1036 个样本 → 26 行）。

这两行的比值约 `60 : 40`，和第 06 课 `--children` 里的 `54.61 : 36.20` 一致。

本课已经生成了一份：[flamegraph.svg](flamegraph.svg)

## 四、怎么看图

- 横轴是样本占比，等价于时间占比；**宽度才是有意义的，左右顺序没有意义**；
- 纵轴是调用深度，从下往上是 `_start → main → handle → decode → mix`；
- 同一个函数出现在不同深度是正常的，那是不同的调用路径；
- 颜色只是区分相邻方块，没有性能含义。

对着本课的图看三件事：

1. 底部很宽的是 `_start`、`libc` 启动代码，接着是 `main`，
   真正有意义的只有 `main` 往上的几层；
2. `main` 上面分成两块：左边是 `handle → decode → mix`，右边是 `render → mix`，
   宽度比就是 6 : 4；
3. 两块的最顶端都是 `mix`，说明两条路最终都停在同一个热点上。

和第 06 课的调用树对比，信息完全一样，只是换了一种排布：
树是按深度缩进的，火焰图是按样本宽度横向铺开的。

## 五、本节收获

- 火焰图不是新数据，只是调用树按样本宽度重新排布；
- 生成只要两条管道命令：先折叠栈，再画图；
- 看图先看宽度占比，再看深度；左右顺序不代表时间先后；
- `--no-inline` 能让图更接近源码结构；
- 同一份 `perf.data` 可以反复生成不同的图，不用重新采样。

到这里"热点在哪、谁调用它"已经能回答。
接下来几课处理采样本身的问题：报告里看不到函数名怎么办、采什么事件才可信。

## 附：跑不出结果时

**1. 提示找不到 `stackcollapse-perf.pl`**

脚本不随 perf 安装，需要单独下载：

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
```

**2. 图里出现 `[unknown]` 或函数名缺失**

`stackcollapse-perf.pl` 会把以 `(` 开头的符号当成进程名跳过。
匿名命名空间里的函数，符号正是 `(anonymous namespace)::foo` 开头。
把函数改成具名命名空间，或者放到全局作用域。

**3. 图底部一大片 `0xffffffff...`**

内核样本。`/proc/kallsyms` 受权限限制，perf 解析不出内核符号。
本课约占 4%，忽略即可。

**4. 折叠栈只有几行 / 图很平**

`perf.data` 里的栈没收全，多半是采样时用了 `-g` 而不是 `--call-graph dwarf`。
帧指针版本会在叶子函数那里断掉，图就少一层。

**5. 想重新采样**

倍率参数和第 06 课一样，两条链一起放大：

```bash
./build/lessons/07_flamegraph/07_flamegraph 2
```

dwarf 展开的数据量增长很快，倍率调大时注意 `perf.data` 的体积。
