# 08 符号与调试信息

> 本课是选学内容：报告里缺函数名或行号时再看，不影响主线分析流程。
> 不熟悉 ELF、符号表这些词没关系，文末有概念卡片。

前面几课的报告里，函数名、文件名、行号都齐全。
但实际拿到的采样数据经常不是这样：要么只剩一串地址，要么有函数名没行号。
这一课只回答一个问题：**perf 报告里的函数名和行号分别从哪来，缺了怎么补。**

## 最短路径

```bash
# 1. 构建（已经建过就跳过）
cmake -S . -B build && cmake --build build -j

# 2. 采样（约 1 秒）
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/08_symbols/08_symbols

# 3. 看结果
perf report --stdio -i perf.data -g none --no-children
```

报告里能看到 `lesson08::walk`，这是正常结果。
本课的重点在后面：把同一个程序换成另外两种构建，再各采一次。

## 一、示例程序

> 不关心程序的具体逻辑没关系，可以直接跳到 [二、同一份源码，三种构建](#二同一份源码三种构建)。

[main.cpp](main.cpp) 只有两个函数：

| 函数 | 写法 | 预期 |
|---|---|---|
| `next` | `always_inline` 的小函数，只做一次表读取 | 被内联进 `walk` |
| `walk` | 在 256 MiB 的表里追指针，1000 万次 | 热点，占九成 |

```cpp
__attribute__((always_inline)) inline std::uint64_t next(std::uint64_t idx) {
  return g_table[idx];          // 第 24 行，真正等内存的读取
}

for (std::uint64_t i = 0; i < rounds; ++i) {
  idx = next(idx);              // 第 32 行，会被展开成一次读取
  sum += idx;
}
```

`next` 只有一条有效指令，编译后不会留下独立函数，
九成时间其实花在它的函数体上。
这样安排是为了演示本课的第三种现象：**符号列说在 `walk`，行号却指到 `next` 里。**

示例程序会打印自己的构建变体：

```
variant: full
walk = 167783687514560
elapsed: 0.942995 s
```

## 二、同一份源码，三种构建

这一节先不碰 perf，只准备实验材料。

本课只改一个变量：**二进制里保留多少符号信息**。
[CMakeLists.txt](CMakeLists.txt) 把同一份 `main.cpp` 编成三个程序：

| 目标 | 编译选项 | 链接后处理 | 调试信息（行号） | 符号表（函数名） |
|---|---|---|---|---|
| `08_symbols` | `-O2 -g` | 无 | 有 | 有 |
| `08_symbols_nodebug` | `-O2 -g0` | 无 | 无 | 有 |
| `08_symbols_stripped` | `-O2 -g` | `strip --strip-all` | 无 | 无 |

为什么要编三个：

- perf 报告里的行号来自调试信息，函数名来自符号表，两者是分开的两样东西；
- 只去掉调试信息，函数名还在；
- 连符号表也去掉，函数名才消失。

三个程序正好对应这三步，下一节各采一次就能看到差别。

先确认构建产物确实如表格所说。`file` 读取 ELF 头部，告诉你有没有调试信息、是否被 strip：

```bash
file build/lessons/08_symbols/08_symbols{,_nodebug,_stripped}
```

```
08_symbols:          ... with debug_info, not stripped
08_symbols_nodebug:  ... not stripped
08_symbols_stripped: ... stripped
```

`nodebug` 和 `stripped` 都没有调试信息，差别在符号表。
`nm` 负责把符号表列出来：`-C` 把 C++ 修饰过的名字还原成可读形式，
`grep lesson08` 只看本课命名空间里的符号：

```bash
nm -C build/lessons/08_symbols/08_symbols_nodebug | grep lesson08
```

```
0000000000001490 T lesson08::walk(unsigned long, unsigned long)
0000000000004010 D lesson08::g_scale
0000000000004160 B lesson08::g_table
```

左边是符号地址，第二列的 `T`、`D`、`B` 是符号类型：
`T` 是代码，`D` 是已初始化数据，`B` 是未初始化数据。
本课只关心代码符号，也就是 `walk` 那一行。

`08_symbols` 输出相同，`08_symbols_stripped` 完全没有输出。
两个未 strip 的版本里都找不到 `next`，因为它被内联进 `walk` 了（第五节解释）。

运行结果第一行的 `variant: full / nodebug / stripped` 是各目标编进去的标记，
用来确认当前跑的是哪个二进制。这三个标记字符串也让三者的 build-id 不同，
属于构建细节，不影响本课结论，原因见附录第 1 条。

## 三、三份报告

三个二进制各采一次，命令只换最后的路径：

```bash
cd build/lessons/08_symbols

taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf \
  -o full.data ./08_symbols
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf \
  -o nodebug.data ./08_symbols_nodebug
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf \
  -o stripped.data ./08_symbols_stripped
```

三次运行都在 0.95 秒左右，各采到 1000 多个样本，`perf.data` 都在 8 MB 上下。
程序运行时长、热点位置完全一样，能变的只有符号信息。

### 完整构建：名字和行号都有

```bash
perf report --stdio -i full.data -g none --no-children
```

```
    90.90%  08_symbols  08_symbols            [.] lesson08::walk(unsigned long, unsigned long)
     2.49%  08_symbols  libc.so.6             [.] __memset_avx2_unaligned_erms
     1.48%  08_symbols  08_symbols            [.] main
```

### 去掉调试信息：只剩函数名和偏移

```bash
perf report --stdio -i nodebug.data -g none --no-children
```

```
    90.93%  08_symbols_node  08_symbols_nodebug    [.] lesson08::walk(unsigned long, unsigned long)
     1.51%  08_symbols_node  08_symbols_nodebug    [.] main
```

函数汇总看不出区别，因为函数名来自符号表，和 `-g` 无关。
区别在行级报告里：

```bash
perf report --stdio -i nodebug.data --sort srcline --no-children -g none
```

```
    90.54%  lesson08::walk(unsigned long, unsigned long)+32
     6.16%  ??:0
     1.27%  memset-vec-unaligned-erms.S:328
```

`+32` 是相对函数入口的字节偏移，换成 `main.cpp:24` 的位置信息没有了。
`walk` 的入口在链接后是 `0x1490`，加上 `0x20` 正好是报告里的 `0x14b0`。

### 剥掉符号表：只剩地址

```bash
perf report --stdio -i stripped.data -g none --no-children
```

```
    90.85%  08_symbols_stri  08_symbols_stripped   [.] 0x00000000000014b0
     1.89%  08_symbols_stri  libc.so.6             [.] __memset_avx2_unaligned_erms
     0.57%  08_symbols_stri  [unknown]             [k] 0xffffffff9e98859a
```

本进程自己的所有函数都变成了地址。`libc` 仍有名字，因为它的动态符号表还在。
行级报告同样只剩地址：

```bash
perf report --stdio -i stripped.data --sort srcline --no-children -g none
```

```
    90.85%  08_symbols_stripped[14b0]
     5.66%  ??:0
     1.89%  memset-vec-unaligned-erms.S:328
```

### 三种状态对照

| 构建 | `Symbol` 列 | `--sort srcline` 列 |
|---|---|---|
| `-O2 -g` | `lesson08::walk` | `main.cpp:24` |
| `-O2 -g0` | `lesson08::walk` | `lesson08::walk+32` |
| `strip --strip-all` | `0x00000000000014b0` | `08_symbols_stripped[14b0]` |

样本占比三次都是九成左右，退化的是**可读性**，不是数据。

## 四、名字和行号从哪来

ELF 文件里有两类互不依赖的信息：

| 信息 | 存放位置 | 由谁生成 | 去掉它 |
|---|---|---|---|
| 函数名、函数入口地址 | `.symtab`（符号表） | 链接 | `strip --strip-all` |
| 文件、行号、内联关系 | `.debug_*`（DWARF） | 编译时的 `-g` | `-g0`，或 `strip --strip-debug` |

perf 的解析顺序是：

1. 采样只记下 IP（指令地址）和所属的 dso；
2. 用 dso 的符号表把 IP 换算成「函数名 + 偏移」；
3. 再用 DWARF 的行号表把 IP 换算成「文件:行号」；
4. 调用栈展开也依赖 DWARF（`--call-graph dwarf`）或帧指针（`-g`）。

所以三种缺法对应三种现象：

- 缺符号表 → 连函数名都没有，只剩地址；
- 缺 DWARF → 有函数名，没有行号；
- 两者都有 → 名字、行号、调用栈都完整。

这也解释了一个常见误解：`strip --strip-debug` 只删调试信息，函数名还在；
`strip --strip-all` 两者都删。本课第三种构建用的是后者。

## 五、内联：名字和行号对不上

完整构建下再按行看：

```bash
perf report --stdio -i full.data --sort srcline --no-children -g none
```

```
    90.23%  main.cpp:24
     5.10%  ??:0
     2.49%  memset-vec-unaligned-erms.S:328
     0.92%  main.cpp:51
```

第 24 行是 [`next` 里的 `return g_table[idx];`](main.cpp#L24-L24)，
但按函数看，九成占比却记在 `walk` 名下。
原因就是第一节说的内联：`next` 没有独立符号，指令被并进了 `walk`。

用 `addr2line` 能把这条关系展开。
`0x14b0` 就是上一节 `walk+32` 对应的地址（`0x1490 + 0x20`）：

```bash
addr2line -f -C -e build/lessons/08_symbols/08_symbols 0x14b0 --inlines
```

```
lesson08::next(unsigned long)
/…/lessons/08_symbols/main.cpp:24
lesson08::walk(unsigned long, unsigned long)
/…/lessons/08_symbols/main.cpp:32
```

（源码路径省略，下同。）

从上往下读：这个地址在 `next` 的第 24 行，
而 `next` 是被内联进 `walk` 第 32 行的那次调用。

坏消息是符号列不会显示 `next`，行号却指向它；
好消息是两者并不矛盾，只是回答的问题不同：

| 列 | 回答的问题 | 受内联影响 |
|---|---|---|
| `Symbol` | 这段机器码属于哪个真实函数 | 有，内联后归属于外层函数 |
| `srcline` | 这段机器码由哪行源码生成 | 没有，仍指向原始行 |

需要热点函数在报告里单独出现时，有两个办法：

- 加 `__attribute__((noinline))`，禁止内联；
- 把函数体放到另一个源文件，跨编译单元默认不会内联（开 LTO 除外）。

代价都是一次真实函数调用的开销。

## 六、拿到没符号的采样怎么办

分析用的二进制不要 strip，strip 只对发布副本做。
对已经拿到的数据，按缺失程度补救：

| 缺什么 | 补救方式 |
|---|---|
| 只有地址 | 找到同 build-id 的未 strip 二进制，`perf buildid-cache --add <文件>` 后再报告 |
| 没有行号 | 重新用 `-g` 编译并采样；已有数据补不回来 |
| 系统库没名字 | 安装发行版的 dbgsym / debuginfo 包，perf 会自动按 build-id 查找 |
| 只想查单个地址 | `addr2line -f -C -e <二进制> <地址>` |

`addr2line` 不依赖 perf.data，只要能拿到构建产物，就能手动翻译地址。
第五节的 `addr2line` 例子就是这种用法。

## 七、本节收获

- `Symbol` 列来自符号表，`srcline` 列来自 DWARF，两者可以独立缺失；
- `-g` 决定有没有行号，`strip --strip-all` 决定有没有函数名；
- `walk+32` 是「函数名 + 字节偏移」，不是行号；
- 内联会让符号归属外层函数、行号仍指向内联函数，这是正常现象；
- 分析用的构建保留 `-g` 且不要 strip，发布副本再另做处理；
- 补救已有采样只能靠同 build-id 的二进制，行号丢了必须重新采样。

到这里，采样数据本身的可信度已经处理好。
下一课换一个角度：采什么事件、采多精确，数字才站得住。

## 附：概念卡片——符号表和调试信息

没接触过这些词，看这一节就够了。

**ELF**：Linux 可执行文件的标准格式，按“段”存放代码、数据、符号表、调试信息。
程序运行时只关心代码和数据，另外两样是给工具看的。

**符号表**：函数名和地址的对照表，链接时生成。
它让 `perf` 能把一个地址翻译成 `lesson08::walk`。
程序运行不依赖它，删掉程序照样跑。

**调试信息**（DWARF）：文件名、行号、变量名、内联关系，编译时加 `-g` 才有。
它让 `perf` 能把地址进一步翻译成 `main.cpp:24`。
程序运行同样不依赖它。

**strip**：把符号表和调试信息一起删掉，用来减小发布体积。
删完之后程序行为不变，但 perf 报告里只剩地址。

**内联**：编译器把一个小函数的指令直接抄进调用者，省掉一次函数调用。
被内联的函数不会单独出现在符号表里，所以报告按函数看是外层函数，
按行看却可能指回内联函数的那一行。

本课的三组对照，就是把这三样东西依次拿掉。

## 附：跑不出结果时

**1. 为什么非要让三个 build-id 不同**

build-id 由链接器生成，内容变了它才变。
`strip` 只删符号表和调试信息，不改代码段，所以 strip 前后 build-id 完全一样。

perf 记录时会按 build-id 把二进制缓存进 `~/.debug`。
先用未 strip 的版本采样，再用同 build-id 的 strip 版本采样，
报告会按 build-id 从缓存里找回未 strip 的原件，把函数名补回来。
现象就是：明明 strip 过了，`Symbol` 列却仍是 `lesson08::walk`。

先确认两个文件是不是同一个 build-id：

```bash
readelf -n build/lessons/08_symbols/08_symbols | grep 'Build ID'
readelf -n build/lessons/08_symbols/08_symbols_stripped | grep 'Build ID'
```

真遇到缓存串用，清掉再采：

```bash
perf buildid-cache --purge build/lessons/08_symbols/08_symbols_stripped
```

或者采样时直接不更新缓存：

```bash
perf record --no-buildid-cache -e cycles:ppp -F 999 --call-graph dwarf \
  -o perf.data ./08_symbols_stripped
```

本课的做法更省事：三个目标各编一个不同的字符串，
build-id 天然不同，缓存不会串用，读者也不用先清缓存。

**2. 报告里出现 `??:0` 或 `[k] 0xffffffff...`**

内核样本。`/proc/kallsyms` 受权限限制，perf 解析不出内核符号。
本课它们只占几个百分点，忽略即可。

**3. 行号偏一两行**

`-O2` 会重排指令，同一条源码可能对应多条指令。
采样时用 `cycles:ppp` 精确归因，能缓解但不能完全消除；
行级结论看趋势，不要抠单行。

**4. 想改运行时长**

参数是倍率：

```bash
./build/lessons/08_symbols/08_symbols 2
```

三个目标都支持同一个参数，改的是 `walk` 的迭代次数。
