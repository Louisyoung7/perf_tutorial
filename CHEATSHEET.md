# perf 常用命令速查

覆盖 01–09 课，完整解释见对应课程。
测试环境见 [README](README.md#测试环境)。

## 最短流程

```bash
# 构建
cmake -S . -B build && cmake --build build -j

# 1. 判断瓶颈类型
taskset -c 2 perf stat \
  -e cpu_core/cycles/,cpu_core/instructions/,cpu_core/branches/,cpu_core/branch-misses/,cpu_core/cache-references/,cpu_core/cache-misses/ \
  ./build/lessons/02_stat_basics/02_stat_basics

# 2. 采样
taskset -c 2 perf record -e cycles:ppp -F 999 --call-graph dwarf -o perf.data \
  ./build/lessons/05_hot_line/05_hot_line

# 3. 按函数
perf report --stdio -i perf.data --no-children -g none

# 4. 按行
perf report --stdio -i perf.data --sort srcline --no-children -g none

# 5. 按调用者
perf report --stdio -i perf.data --children -g none --no-inline
```

只看函数或行时，第 2 步可以去掉 `--call-graph dwarf`。

## 参数速记

| 参数 | 作用 |
|---|---|
| `taskset -c 2` | 绑定 2 号 CPU，避免大小核迁移 |
| `-e` | 选择事件；`cpu_core/` 只数 P 核 |
| `-F 999` | 每秒约 999 个样本；`-c N` 改为每 N 个事件采一次 |
| `--call-graph dwarf` | 完整调用栈；采样时 `-g` 按帧指针，可能漏层 |
| `-o` / `-i` | 采样输出文件 / 报告输入文件，默认都是 `perf.data` |
| `--stdio` | 纯文本输出，不进全屏界面 |
| `--no-children` / `--children` | 只看自身 / 自身加子调用 |
| `-g none` | 报告不打印调用链 |
| `--no-inline` | 内联代码不单独成行，报告更短 |
| `--sort srcline` | 按源码行汇总；`--sort comm,srcline` 保留进程名 |
| `--percent-limit N` | 只显示占比不低于 N% 的条目；默认 0 |

三个比值：

| 指标 | 算法 | 判断 |
|---|---|---|
| IPC | `instructions / cycles` | 3~4 正常；1 左右查分支；远低于 1 查内存 |
| 末级未命中率 | `cache-misses / cache-references` | 高且绝对次数多才是瓶颈 |
| 分支错误率 | `branch-misses / branches` | 10% 以上查分支 |

事件速记：`cycles` 时间、`instructions` 指令数、`cache-misses` 等内存事件、`branch-misses` 分支、`task-clock` CPU 时间（毫秒）。
行级或指令级结论用 `cycles:ppp`；样本太少就调高 `-F` 或延长运行时间。

## 按问题查命令

| 问题 | 在最短流程上改 | 课程 |
|---|---|---|
| 瓶颈在哪一类 | 第 1 步 | [02](lessons/02_stat_basics/README.md)、[03](lessons/03_memory_wait/README.md)、[04](lessons/04_branch_miss/README.md) |
| 哪一行 | 第 4 步 | [05](lessons/05_hot_line/README.md) |
| 谁调用、各占多少 | 第 5 步 | [06](lessons/06_callers/README.md) |
| 调用树 | `perf report --stdio -i perf.data --no-inline --percent-limit 3` | [06](lessons/06_callers/README.md) |
| 哪条指令 | `perf annotate --stdio -i perf.data` | [05](lessons/05_hot_line/README.md)、[09](lessons/09_events/README.md) |
| 火焰图 | 见下一节 | [07](lessons/07_flamegraph/README.md) |
| 缺函数名或行号 | 检查 `-g`、符号表、build-id | [08](lessons/08_symbols/README.md) |
| 样本准不准 | `cycles:ppp`、`-F`、`-c` | [09](lessons/09_events/README.md) |

## 火焰图

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git /tmp/FlameGraph
perf script -i perf.data --no-inline | /tmp/FlameGraph/stackcollapse-perf.pl > perf.folded
/tmp/FlameGraph/flamegraph.pl perf.folded > flamegraph.svg
```

## 常见问题

| 现象 | 处理 |
|---|---|
| `cycles:ppp` 不支持 | 换 `cycles:pp`，再不行用 `cpu-clock` |
| 只有地址，没有函数名 | 用同 build-id 的未 strip 文件：`perf buildid-cache --add <文件>` |
| 有函数名，没有行号 | 加 `-g` 重新编译并重采 |
| 热点落在 `retq` 或右花括号 | 采样打滑，改用 `cycles:ppp` 重采 |
| 样本只有几十个 | 提高 `-F` 或延长运行时间 |
| 报告混进 `cpu_atom` | `taskset` 绑核，事件加 `cpu_core/` 前缀 |
| `??:0`、`[k] 0xffffffff...` | 内核符号受限，占比小可忽略 |
| `Maximum frequency rate ... exceeded` | 内核限流，查 `perf_event_max_sample_rate` |

## 还没覆盖

`perf top`、`perf list`、`perf diff`、`perf trace`、`perf script` 自定义统计等，见 [OUTLINE.md](OUTLINE.md)。
