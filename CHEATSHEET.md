# perf 常用命令速查

覆盖 01–09 课已经讲过的命令，按“先判断、再定位、最后看图”的顺序排列。
所有命令都在仓库根目录执行，`./build/...` 是 CMake 产物路径。
测试环境见 [README 的测试环境表](README.md#测试环境)；换机器后绝对数字会变，比值仍然成立。

## 按问题选命令

| 我想知道 | 用哪个 | 对应课程 |
|---|---|---|
| 程序是算得慢，还是在等 | `perf stat` | [02](lessons/02_stat_basics/README.md)、[03](lessons/03_memory_wait/README.md)、[04](lessons/04_branch_miss/README.md) |
| 时间花在哪个函数 | `perf report` 默认视图 | [05](lessons/05_hot_line/README.md) |
| 时间花在哪一行 | `perf report --sort srcline` | [05](lessons/05_hot_line/README.md) |
| 热点被谁调用，各占多少 | `--call-graph dwarf` + `--children` | [06](lessons/06_callers/README.md) |
| 调用关系怎么分享 | 火焰图 | [07](lessons/07_flamegraph/README.md) |
| 具体是哪条指令 | `perf annotate` | [05](lessons/05_hot_line/README.md)、[09](lessons/09_events/README.md) |
| 报告里没有函数名或行号 | 检查 `-g`、符号表、build-id | [08](lessons/08_symbols/README.md) |
| 样本准不准、够不够 | `cycles:ppp`、`-F`、`-c` | [09](lessons/09_events/README.md) |

## 最短流程

```bash
# 0. 构建
cmake -S . -B build && cmake --build build -j

# 1. 判断瓶颈类型（以 02 课为例）
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_basics/02_stat_basics

# 2. 采样（以 05 课为例）
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line

# 3. 按函数看
perf report --stdio -i perf.data --no-children -g none

# 4. 按行看
perf report --stdio -i perf.data --sort srcline --no-children -g none

# 5. 按调用者看
perf report --stdio -i perf.data --children -g none --no-inline
```

流程里反复出现的参数：

- `taskset -c 2`：把进程固定在 2 号 CPU 上，避免大小核迁移；
- `-e`：选择要统计或采样的事件；
- `cpu_core/`：只数 P 核的计数器；
- `-F 999`：每秒采约 999 次；
- `-o`：指定采样结果写到哪个文件；
- `-i`：指定报告读取哪个采样文件；
- `--stdio`：纯文本输出，不进全屏界面。

`-o` 和 `-i` 省略时默认都读写当前目录的 `perf.data`。
`-e` 省略时用 perf 的默认事件，正式对比建议显式写。

只看函数或行时，可以去掉 `--call-graph dwarf`，采样文件会小很多。
需要调用关系时再加回来。

## 1. 环境自检

```bash
perf --version
cat /proc/sys/kernel/perf_event_paranoid   # 需要 <= 1
```

`perf_event_paranoid` 大于 1 时采不到硬件计数器，处理办法见 [01 课附录](lessons/01_environment/README.md#附跑不出结果时)。

编译选项固定为 `-O2 -g -fno-omit-frame-pointer`：

- `-O2` 让程序接近发布版本；
- 编译选项里的 `-g` 生成行号信息，报告里的 `main.cpp:27` 依赖它；
- `-fno-omit-frame-pointer` 保留帧指针，给采样时的 `-g` 展开调用栈用。

## 2. perf stat：先看比值

```bash
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_basics/02_stat_basics
```

| 参数 | 作用 |
|---|---|
| `taskset -c 2` | 把进程固定在 2 号 CPU 上，避免大小核迁移；测试环境 0–15 是 P 核 |
| `-e` | 要统计的事件，逗号分隔 |
| `cpu_core/` | 只数 P 核的计数器；不带前缀时同名事件会同时展开到 P 核和 E 核 |

`cpu_core/` 是测试环境这块 Intel CPU 的 PMU 名称。
换到没有该 PMU 的机器上，去掉前缀用通用事件名。

六个事件的原始计数要换算成比值：

| 比值 | 算法 | 怎么判断 |
|---|---|---|
| IPC | `instructions / cycles` | 3~4 没有明显瓶颈；1 左右查分支；远低于 1 查内存、锁、IO |
| 末级未命中率 | `cache-misses / cache-references` | 高且绝对次数多，才是瓶颈 |
| 分支错误率 | `branch-misses / branches` | 10% 以上先查分支；循环分支会稀释比例 |

更短的通用写法：

```bash
taskset -c 2 perf stat \
  -e task-clock,cycles,instructions,cache-references,cache-misses \
  ./build/lessons/09_events/09_events
```

`task-clock` 的单位是毫秒，`cycles` 的单位是周期数，两者不能直接比较。
通用名会同时展开到两个 PMU，输出里没跑的那个核会显示 `<not counted>`。

## 3. perf record + report：从函数到行

### 采样

```bash
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line
```

| 参数 | 作用 |
|---|---|
| `-e cycles:ppp` | 采 CPU 周期；`ppp` 是精确采样，让样本落在真正触发事件的指令上 |
| `-F 999` | 每秒采约 999 次；数字越大越精细，开销和文件也越大 |
| `-c 200000` | 每 20 万个事件采一次，和 `-F` 二选一 |
| `--call-graph dwarf` | 按调试信息展开调用栈，叶子函数的上一层不会丢 |
| `-g` | 按帧指针展开调用栈，更轻，但会漏掉没有帧指针的叶子函数 |
| `-o perf.data` | 采样结果写入指定文件 |

这里 `-g` 是 `perf record` 的参数，和编译选项里的 `-g` 是两回事。

### 四种报告视图

| 目的 | 命令 |
|---|---|
| 按函数 | `perf report --stdio -i perf.data --no-children -g none` |
| 按行 | `perf report --stdio -i perf.data --sort srcline --no-children -g none` |
| 按调用者 | `perf report --stdio -i perf.data --children -g none --no-inline` |
| 调用树 | `perf report --stdio -i perf.data --no-inline --percent-limit 3` |

同一份 `perf.data` 可以换参数反复看，不用重新采样。

| 参数 | 作用 |
|---|---|
| `--stdio` | 纯文本输出，不进全屏界面 |
| `-i perf.data` | 指定采样文件，对应采样时的 `-o` |
| `--no-children` | 只算函数自身的时间 |
| `--children` | 自身加子调用，多一列 `Children` |
| `-g none` | 不打印调用链 |
| `--sort` | 整组替换排序列，如 `srcline`、`comm,srcline` |
| `--no-inline` | 内联代码不单独成行，报告更短 |
| `--percent-limit 3` | 只显示占比不低于 3% 的条目，默认 0（全部显示） |

读表看这些列：

- 默认视图的 `Overhead`：该行占全部样本的比例，可以当成耗时占比；
- `--children` 视图的 `Self`：只算函数自身的样本；
- `--children` 视图的 `Children`：自身加所有子调用的样本，回答“这条路径总共占多少”。

报告头也要看：`# Samples` 是样本条数，`of event '...'` 是采样用的事件。
事件名带 `ppp` 说明这批样本是精确采样。

符号列里的 `[.]` 是用户态代码，`[k]` 是内核代码。
`??:0` 和 `[k] 0xffffffff...` 通常是内核符号受权限限制，占比小可以忽略。
报告默认列出所有采到样本的行。
`--percent-limit 0.5` 可以只保留占比不低于 0.5% 的行。

## 4. perf annotate：定位到指令

```bash
perf annotate --stdio -i perf.data
perf annotate --stdio -i perf.data --no-source
```

- 每行三段：左边是样本占比，中间是地址，右边是汇编指令；
- 看占比堆在哪条指令；
- `--no-source` 只看汇编，不混排源码。

热点落在 `retq` 或右花括号上，说明采样打滑（skid）。
改用 `cycles:ppp` 重新采样，样本才会回到真正等内存的加载指令。

## 5. 火焰图

```bash
# 脚本只需下载一次
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph

# 折叠调用栈
perf script -i perf.data --no-inline \
  | /tmp/FlameGraph/stackcollapse-perf.pl > perf.folded

# 画图
/tmp/FlameGraph/flamegraph.pl perf.folded > flamegraph.svg
```

用浏览器打开 `flamegraph.svg`。

- 横轴是样本占比，宽度有意义，左右顺序没有意义；
- 纵轴是调用深度，从下往上是调用方向；
- 颜色只用来区分相邻方块，没有性能含义；
- `--no-inline` 让内联函数单独成层，更接近源码结构。

## 6. 事件与采样密度

### 按问题换事件

| 想回答 | 事件 | 单位 |
|---|---|---|
| 程序在 CPU 上待了多久 | `task-clock` | 毫秒 |
| CPU 跑了多少圈 | `cycles` | 周期数 |
| 干了多少活 | `instructions` | 指令条数 |
| 是否在等内存 | `cache-references`、`cache-misses` | 次 |
| 是否分支猜错 | `branches`、`branch-misses` | 次 |

### 采样密度

| 写法 | 含义 | 适用场景 |
|---|---|---|
| `-F 97` | 每秒约 97 次 | 长时间运行，想减小开销 |
| `-F 999` | 每秒约 999 次 | 默认值，跑 1 秒约 1000 个样本 |
| `-F 5000` | 每秒约 5000 次 | 热点只持续几毫秒 |
| `-c 200000` | 每 20 万个事件采一次 | 样本数要与事件数严格成比例 |

`-F` 超过内核上限会被自动降频，并打印警告，例如：

```
warning: Maximum frequency rate (12,000 Hz) exceeded, throttling from 100,000 Hz to 12,000 Hz.
```

上限见 `cat /proc/sys/kernel/perf_event_max_sample_rate`。
想让它直接报错，加 `--strict-freq`。

### 精确采样

| 写法 | 落点 | 结论 |
|---|---|---|
| `cycles` | 常落到下一条指令，甚至右花括号 | 不精确 |
| `cycles:p` | 仍可能落偏 | 不保证 |
| `cycles:pp` | 多数落在加载指令 | 通常够用 |
| `cycles:ppp` | 落在加载指令 | 推荐日常使用 |

`p` 最多写三个，`cycles:pppp` 会报 `Maximum precise value is 3`。
能到哪一级由 CPU 和事件决定，换机器要重新确认落点。

`perf record` 不带 `-e` 时，默认可能已经带精确级别。
报告头写成 `cpu_core/cycles/P`（大写 P）就表示 precise_ip=2。
显式写 `-e cycles` 会关掉这份默认精确度。

## 7. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `cycles:ppp` 显示 `<not supported>` | CPU 或虚拟机不支持精确采样 | 换 `cycles:pp`；仍不行换 `cpu-clock`，只保留函数级结论 |
| 报告里出现 `??:0`、`[k] 0xffffffff...` | 内核符号受权限限制 | 占比小可忽略 |
| 只有地址，没有函数名 | 二进制被 `strip` | 用同 build-id 的未 strip 版本：`perf buildid-cache --add <文件>` |
| 有函数名，没有行号 | 编译时没加 `-g` | 加 `-g` 重新编译并重新采样，已有数据补不回来 |
| 行号整体偏一行 | 采样打滑（skid） | 用 `cycles:ppp` 重采 |
| 样本只有几十个 | 程序太短或频率太低 | 提高 `-F`，或延长运行时间 |
| 报告里混进 `cpu_atom` | 进程在大小核之间迁移 | 用 `taskset` 绑核，并写 `cpu_core/` 前缀 |
| strip 过的程序仍有函数名 | perf 按 build-id 从缓存找回了旧二进制 | `perf buildid-cache --purge <文件>`，或采样时加 `--no-buildid-cache` |
| `perf.data` 特别大 | `--call-graph dwarf` 的栈数据比样本大得多 | 正常现象；不需要调用链时去掉该参数 |

## 8. 还没覆盖的命令

下面这些常用命令还没有专门课程，按需再补：

| 命令 | 用途 | 对应计划 |
|---|---|---|
| `perf top` | 实时看热点函数 | 暂无 |
| `perf list` | 列出当前机器支持的事件 | 暂无 |
| `perf diff` | 对比两次采样的差异 | 18 课 |
| `perf trace` | 跟踪系统调用 | 15 课 |
| `perf script` | 导出样本做自定义统计 | 17 课 |
| `perf mem`、`perf lock`、`perf sched` | 访存、锁、调度专项分析 | 11、13、14 课 |

课程安排见 [OUTLINE.md](OUTLINE.md)。
