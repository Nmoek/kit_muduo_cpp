# Benchmark分析步骤学习(以当前日志系统为例)

	
## 1. 拆分测试路径，选好要隔离的接口

性能分析的第一步不是运行工具，而是先把调用链拆成可以解释的实验。当前同步日志路径是：

```text
KIT_DEBUG(logger, module)
    -> Logger::shouldLog()
    -> 构造 LogAttr
    -> LogAttrWrap 析构提交
    -> Logger::log()
    -> Logger 路由锁
    -> LogAppender::append()
    -> Appender 锁
    -> LogFormatter::format()
    -> ConsoleAppender/FileAppender
    -> stdout/ofstream/flush
```

当前 benchmark 按成本逐层增加：

| 用例 | 包含的成本 | 排除的成本 |
|---|---|---|
| `BM_BaselineLoop` | benchmark 迭代循环、计时器和优化屏障 | 整个日志系统 |
| `BM_LevelFiltered` | `KIT_LOGGER`、logger map、`loggers_mtx_`、`shouldLog` | `LogAttr`、formatter、sink、I/O |
| `BM_LevelFilteredCached` | cached `Logger::Ptr`、shared_ptr 拷贝、`shouldLog`、appender 锁 | logger map、`loggers_mtx_`、`LogAttr`、I/O |
| `BM_LevelFilteredPerfStat` | 固定一亿次 logger 查找和过滤 | 自动校准的迭代次数 |
| `BM_LevelFilteredCachedPerfStat` | 固定一亿次缓存 logger 过滤 | 自动校准的迭代次数 |

这样才能用：

```text
BM_LevelFiltered - BM_LevelFilteredCached
```

近似隔离 `LogManager::getLogger()`、hash、字符串比较和 logger map 锁。这个差值是实验假设，不是数学上完全独立的函数耗时，最终要用 perf 调用栈验证。

每个用例都必须在注释中明确：测试思路、包含范围、排除范围和正确性预期。否则不同用例的数值不能直接比较。

## 2. 使用空循环测定 benchmark 底噪和环境稳定性

空循环的正确目的不是直接判断 CPU 负载，而是测量：

```text
Google Benchmark 迭代循环
    + 计时器
    + DoNotOptimize 编译器屏障
    + 最小循环本身
```

CPU 负载应另外观察：

```bash
uptime
cat /proc/loadavg
```

以及 benchmark JSON 中的 `load_avg`。空循环主要用于判断框架底噪和多轮结果是否稳定。

当前基线：

```cpp
void BM_BaselineLoop(benchmark::State& state)
{
    std::uint64_t value = 0;

    for(auto _ : state)
    {
        (void)_;
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BaselineLoop);
```

`benchmark::State` 控制迭代和计时；`for(auto _ : state)` 不是固定次数循环，框架会自动校准；`DoNotOptimize` 防止编译器删除整个循环；`SetItemsProcessed` 让报告包含吞吐率。

空循环不是零成本函数，也不能机械地从日志耗时中扣除。例如 `0.284 ns` 只说明日志用例明显高于框架底噪，不代表一次函数调用真的只需要 0.284 ns。报告的是大量迭代的平均吞吐，CPU 流水线可以重叠多次迭代。

验收建议：

```text
重复运行 5~7 次
优先看 median
CV < 5% 才认为当前测量稳定
CV 高时先排查负载、CPU 亲和性和后台任务
```

## 3. 用相同实验条件比较隔离接口

benchmark 参数和环境必须保持一致：

```text
相同代码版本和工作区状态
相同 CMAKE_BUILD_TYPE 和优化参数
相同 -g、-fno-omit-frame-pointer
相同 CPU 亲和性
相同 benchmark filter 语义
相同 min_time、repetitions
相同固定 iterations
相同输出 sink 和文件系统
```

自动校准版用于平均耗时和吞吐：

```bash
./bin/log_benchmark \
    --benchmark_filter='^(BM_BaselineLoop|BM_LevelFiltered)$' \
    --benchmark_min_time=0.5 \
    --benchmark_repetitions=7 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/kit-log-bench/level-filtered.json \
    --benchmark_out_format=json
```

正则的 `^` 和 `$` 很重要。`BM_LevelFiltered` 不加锚点时会同时匹配 cached 和 PerfStat 变体。

固定次数版用于 perf 的 per-op 比较：

```cpp
BENCHMARK(BM_LevelFiltered)
    ->Name("BM_LevelFilteredPerfStat")
    ->Iterations(100'000'000);
```

`benchmark_repetitions=7` 是 Google Benchmark 内部重复；`perf stat -r 3` 是 perf 外层重新启动整个进程，二者不要叠加到无法解释。

文件和 stdout 用例还必须保持 sink 语义一致：discard appender 只用于隔离上层成本，不能拿它的绝对耗时推导 FileAppender 的耗时；stdout 应重定向到 `/dev/null`，避免终端渲染成为热点；文件结果应写 WSL ext4 的 `/tmp`，不要放到 `/mnt/c`。

结果输出的核心字段：

| 字段 | 含义 |
|---|---|
| `Time` | wall-clock 每次迭代时间，包含阻塞和被调度出去的时间 |
| `CPU` | 进程真正占用 CPU 的每次迭代时间 |
| `mean/median/stddev/cv` | 多轮统计结果 |
| `items_per_second` | `SetItemsProcessed` 计数除以测量时间 |
| JSON `time_unit` | `real_time/cpu_time` 的单位 |

当前没有调用 `UseRealTime()`，因此 rate counter 默认按 CPU 时间计算。`iterations=7` 出现在聚合行时表示 7 轮重复，不表示内部只执行 7 次。

## 4. 分层使用 perf：计数、采样、调度验证

perf 不能用一条命令回答所有问题，需要区分三层：

```text
perf stat
    -> 总 cycles、instructions、cache、branch、调度计数

perf record/report/annotate
    -> CPU 样本落在哪些函数和源码指令

perf sched、futex tracepoint、strace
    -> 是否真的发生调度等待、锁竞争或 syscall 阻塞
```

### 4.1 perf stat 计算总成本

当前 WSL2 使用实际安装的用户态 perf：

```bash
export PERF_BIN=/usr/lib/linux-tools/5.15.0-187-generic/perf
```

固定一亿次 logger 查找：

```bash
"$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredPerfStat$' \
        >/dev/null
```

缓存 logger 对照：

```bash
"$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null
```

`-r 3` 只表示完整进程重复三次；相同操作次数来自 C++ 的 `->Iterations(100'000'000)`。因此：

```text
cycles/op = cycles / 100000000
instructions/op = instructions / 100000000
IPC = instructions / cycles
```

核心事件：

| 事件 | 解释 |
|---|---|
| `task-clock` | 进程实际占用 CPU 的时间 |
| `cycles` | CPU 时钟周期 |
| `instructions` | retired instructions |
| `branches`/`branch-misses` | 分支数量和预测失败 |
| `cache-references`/`cache-misses` | cache 参考和未命中 |
| `context-switches` | 调度切换次数，不等于 mutex 等待次数 |
| `cpu-migrations` | 线程迁移 CPU 次数 |
| `page-faults` | 缺页事件 |

`cycles:u` 中的 `:u` 表示只统计用户态。`<not supported>` 是内核/虚拟化不支持；`<not counted>` 表示事件没有获得有效计数。事件过多时可能 multiplex，应检查 enabled/running 比例。

### 4.2 perf record/report/annotate 定位热点

```bash
"$PERF_BIN" record \
    -F 999 \
    -g \
    --call-graph fp \
    -o /tmp/kit-log-bench/lookup-cached.perf.data \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null
```

参数含义：

| 参数 | 含义 |
|---|---|
| `-F 999` | 约每秒 999 次采样，不是每秒调用 999 次 |
| `-g` | 记录调用栈 |
| `--call-graph fp` | 使用 frame pointer 展开调用栈 |
| `-o` | 指定 perf.data |

前提是 benchmark 和 `libkit_muduo.so` 都包含 `-g -fno-omit-frame-pointer`。`Overhead` 是样本占比，近似 CPU 时间比例，不是精确调用次数。

报告：

```bash
"$PERF_BIN" report \
    --stdio \
    --no-children \
    --percent-limit 0.5 \
    --sort=dso,symbol \
    -i /tmp/kit-log-bench/lookup-cached.perf.data \
    > tests/benchmarks/log/result/lookup-cached.perf.parsed.txt
```

`--no-children` 不让父函数重复累加子函数 overhead，但调用树仍然可以显示。`Samples` 是采样数量，不是调用次数；`Event count` 是整个运行期间 cycles 等事件的估算值；`Lost Samples: 0` 表示 perf 缓冲区没有丢样本。

annotate：

```bash
"$PERF_BIN" annotate \
    --stdio \
    --symbol='kit_muduo::Logger::shouldLog' \
    -i /tmp/kit-log-bench/lookup-cached.perf.data \
    > tests/benchmarks/log/result/lookup-cached.shouldlog.annotate.txt
```

它把采样下钻到源码和汇编指令。看到 `pthread_mutex_lock/unlock` 只能说明 CPU 样本落在锁路径，不能单独证明发生了严重阻塞。

### 4.3 用其他工具确认调度和锁竞争

`context-switches` 高，只能说明调度切换多；要确认线程是否真的在等待 mutex/futex，使用：

```bash
strace -f -c ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
    >/dev/null
```

关注 `calls`、`total time`、`usecs/call` 和 futex/write/open 等 syscall。

也可以使用：

```bash
sudo "$PERF_BIN" sched record -- \
    ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null

sudo "$PERF_BIN" sched timehist -i perf.data
```

`timehist` 中的 wait time、scheduling delay 和 run time 可以解释 `real_time` 明显大于 `CPU` 的情况。无竞争的用户态 pthread mutex 可能不会进入 futex，因此还应结合 bpftrace futex tracepoint 和多线程 benchmark。

---

## 5. 日志系统关键性能问题验证与优化

### 5.1 测试过程：模块 Logger 缓存单线程 A/B 基线

本章记录本轮单线程实验从提出问题、排除干扰因素、设计正式 A/B 用例，到使用 Google Benchmark、`perf stat` 和 `perf record/report` 交叉验证的完整过程。这里记录的是一次具体实验，不替代前面的方法说明。

#### 5.1.1 测试目标与待验证假设

本轮正式目标来自日志系统评价文档 4.6：模块日志宏每次执行时都会通过 `KIT_LOGGER("name")` 进入 `LogManager::getLogger()`。即使 Logger 已经存在，旧路径仍然重复执行：

```text
LogManager::GetInstance()
    -> loggers_mtx_.lock()
    -> unordered_map hash/bucket 查找
    -> 字符串比较
    -> Logger::Ptr 拷贝
    -> loggers_mtx_.unlock()
```

需要验证的核心假设是：

```text
对于名称固定且生命周期覆盖整个进程的模块 Logger，
将 Logger::Ptr 缓存在函数内 static 中，
可以从每次日志调用的热路径中移除 LogManager 查找和 loggers_mtx_。
```

本轮只评价模块 Logger 的获取成本以及它对“日志被级别过滤”路径的影响，不评价 formatter、Appender、文件 I/O、flush 或物理持久化。

新旧路径的语义如下：

```text
Legacy:
    LogManager::GetInstance().getLogger("base")

Cached:
    log_detail::GetLoggerHelper("base")
        -> GetBaseLogger()
        -> 函数内 static Logger::Ptr
```

生产宏由 `MUDUO_LOG_CACHE_MODULE_LOGGER` 隔离：

```cpp
#if MUDUO_LOG_CACHE_MODULE_LOGGER
#define KIT_LOGGER(NAME) \
    kit_muduo::log_detail::GetLoggerHelper(NAME)
#else
#define KIT_LOGGER(NAME) \
    kit_muduo::LogManager::GetInstance().getLogger(NAME)
#endif
```

函数内 static 首次初始化仍会进入 `LogManager` 并获取 `loggers_mtx_`；本轮要消除的是初始化完成后每次日志调用都重复查找和加锁的成本。`LogManager` 的锁仍用于动态添加 Logger、未知名称慢路径和配置修改，不能因为缓存路径更快就全局删除。

#### 5.1.2 工具和构建环境核对

本轮环境核对结果：

| 项目 | 结果 | 本轮意义 |
|---|---|---|
| 系统 | WSL2，Linux `6.18.33.2-microsoft-standard-WSL2` | 性能绝对值只代表当前 WSL2 环境 |
| CPU | 12 个逻辑 CPU | 单线程正式测试固定到 CPU 2 |
| Google Benchmark | 系统包 `libbenchmark-dev`，运行库 1.6.1 | CMake 可以找到 `benchmark::benchmark` |
| perf | `/usr/lib/linux-tools/5.15.0-187-generic/perf`，版本 5.15.209 | `/usr/bin/perf` 与 WSL 自定义内核版本不匹配，因此使用实际安装文件 |
| PMU | cycles、instructions、branches、cache 事件可计数 | 本轮可以使用硬件计数器，不必退化为纯软件事件 |
| `perf_event_paranoid` | `2` | 普通采样可用，部分场景使用 `sudo` |
| bpftrace | 0.14.0，tracepoint/kprobe/perf_event 可用 | 后续用于 futex/syscall 验证，不作为本轮最终成绩 |

bpftrace 0.14 的 smoke test 已确认 interval 和 syscall tracepoint 可以工作；`BEGIN` 和 `cpid` 在当前版本存在兼容性问题，因此本轮正式 CPU 热点分析使用 perf。bpftrace 的版本限制不会影响本章 Google Benchmark 和 perf 数据。

性能构建使用独立目录：

```bash
cmake -S . -B build-perf -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DMUDUO_BENCH.LOG=ON

cmake --build build-perf \
    --target log_benchmark \
    -j4
```

编译命令需要同时满足：

```text
启用优化：最终生效为 -O2
保留调试信息：-g
关闭 assert 路径：-DNDEBUG
保留 frame pointer：-fno-omit-frame-pointer
```

接入目标后的第一次构建使用了 `-DCMAKE_BUILD_TYPE=debug`，`compile_commands.json` 显示 `-O0`。该步骤只证明 `find_package(benchmark)`、目标链接和源码编译成功，未作为性能数据。确认这一点后重新配置性能目录，直到编译命令满足上述条件才开始记录正式基线。

如果命令行同时出现项目全局 `-O3` 和 RelWithDebInfo 的 `-O2`，GCC 以后出现的选项为准，因此该次构建最终按 `-O2` 优化。Debug/`-O0` 结果只能用于功能排查，不能和本章数据混用。

#### 5.1.3 早期探索：识别名称构造干扰

正式转向 4.6 问题之前，先通过 `BM_LevelFiltered`、cached Logger 和长短名称做了探索。这些实验的价值是学习如何识别 benchmark 中的混杂变量，不作为最终模块缓存收益。

空循环基线在 7 轮重复中得到：

```text
BM_BaselineLoop median CPU: 0.262 ns/op
BM_BaselineLoop CPU CV    : 0.52%
```

这说明后续几十纳秒的 Logger 路径明显高于 benchmark 循环底噪。不能把 `0.262 ns` 机械地从其他结果中扣除，因为流水线和迭代结构并不满足简单相减条件。

最初普通查找与直接缓存 Logger 的结果为：

```text
BM_LevelFiltered median CPU       : 65.4 ns/op
BM_LevelFilteredCached median CPU :  9.32 ns/op
```

随后发现测试名称本身可能引入临时 `std::string` 构造。短名称预构造实验：

```text
BM_LevelFiltered median CPU             : 55.9 ns/op
BM_LevelFilteredPreparedName median CPU : 54.6 ns/op
```

两者只相差约 `1.3 ns`，说明短名称处于 SSO 范围时，预构造收益很小。长名称实验则为：

```text
BM_LevelFilteredLongName median CPU         : 76.5 ns/op
BM_LevelFilteredLongNamePrepared median CPU : 54.0 ns/op
```

长名称预构造减少约 `22.5 ns/op`，证明长字符串分配和构造会污染 Logger lookup 的测量。由此得到两个实验约束：

```text
1. 正式 A/B 必须使用同一个已注册模块名，不能让名称长度成为变量。
2. 名称构造实验只作为学习引子，不能替代对 4.6 模块 Logger 重复查找问题的测量。
```

#### 5.1.4 正式 A/B 用例设计

正式测试选择已注册的 `base` Logger：

```cpp
constexpr char kModuleLoggerName[] = "base";
```

选择 `base` 有两个目的：一是确保 Legacy 和 Cached 最终得到同一个 Logger 对象；二是确保 cached 路径命中已注册 provider，不退化到未知名称慢路径。它位于 `GetLoggerHelper` 的第一个分支，因此当前结果代表 provider 分发的较好情况，后续还应使用靠后分支补充最差侧数据。

正式用例分为两层：

| 用例 | 每次迭代包含 | 刻意排除 |
|---|---|---|
| `BM_ModuleLoggerAccessLegacy` | 单例访问、`loggers_mtx_`、hash/map 查找、字符串比较、`shared_ptr` 拷贝 | `shouldLog`、`LogAttr`、formatter、Appender、I/O |
| `BM_ModuleLoggerAccessCached` | `GetLoggerHelper` 分支、provider 调用、静态 `Logger::Ptr` 读取和拷贝 | 首次 static 初始化、LogManager、map、mutex、日志生成和 I/O |
| `BM_ModuleLevelFilteredLegacy` | Legacy Logger 获取、`KIT_DEBUG` 宏控制流、`shouldLog` | `LogAttr`、formatter、Appender、I/O |
| `BM_ModuleLevelFilteredCached` | Cached Logger 获取、`KIT_DEBUG` 宏控制流、`shouldLog` | LogManager 查找、`LogAttr`、formatter、Appender、I/O |

每个函数同时注册自动校准版本和固定一亿次版本：

```text
自动校准版本：Google Benchmark 的 median、CV、吞吐率
PerfStat 版本：固定 100,000,000 次，计算 cycles/op 等派生指标
```

为了让两条路径在完全相同的二进制、链接布局和运行环境中对照，benchmark 显式调用 Legacy 和 Cached 接口，而不是依靠分别构建两个宏版本。生产宏是否能正确切换仍需在正式发布验收时分别构建验证，但不会干扰本章微基准的内部 A/B。

计时循环前先完成预热：

```cpp
(void)LogManager::GetInstance().getLogger(kModuleLoggerName);
(void)log_detail::GetLoggerHelper(kModuleLoggerName);
```

因此正式测量不包含 `LogManager` 单例首次构造、默认配置加载或函数内 static 首次初始化。两边测量的都是稳态热路径。

Logger 级别设置为 `ERROR`，被测日志使用 `DEBUG`：

```text
DEBUG < ERROR
    -> shouldLog() 返回 false
    -> 不构造 LogAttr
    -> 不进入 formatter/Appender/I/O
```

这保证 `LevelFiltered` 两组只比 `Logger 获取 + shouldLog`，不会被消息构造和 sink 成本淹没。

#### 5.1.5 单线程实验控制和执行命令

正式 Google Benchmark 使用 CPU 亲和性固定到 CPU 2，避免运行过程中迁移 CPU：

```bash
taskset -c 2 ./bin/log_benchmark \
    --benchmark_filter='^BM_Module(LoggerAccess|LevelFiltered)(Legacy|Cached)$' \
    --benchmark_min_time=0.5 \
    --benchmark_repetitions=7 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=tests/benchmarks/log/result/module-logger-ab-single.json \
    --benchmark_out_format=json
```

运行时环境：

```text
时间         : 2026-08-12T02:16:48+08:00
逻辑 CPU     : 12
报告频率     : 2208 MHz
Load Average : 0.35, 0.42, 0.64
线程数       : 1
重复轮数     : 7
```

选择这些参数的原因：

| 控制项 | 目的 |
|---|---|
| `taskset -c 2` | 保持单线程 CPU 亲和性一致 |
| `min_time=0.5` | 每个实例累计足够长，降低计时器和启动噪声占比 |
| `repetitions=7` | 得到 median、stddev 和 CV，不依赖单次结果 |
| 完整名称正则 | 避免误运行 `PerfStat` 固定一亿次变体 |
| 同一进程四个用例 | 减少不同构建、不同共享库布局和不同 CPU 状态造成的偏差 |

正式比较优先使用 CPU median。`real_time` 用于观察调度或阻塞对业务线程的影响，吞吐由当前 counter 的 CPU 时间计算。

#### 5.1.6 Google Benchmark 结果与指标分析

正式单线程结果：

| 路径 | Real median | CPU median | 吞吐率 | Real CV | CPU CV |
|---|---:|---:|---:|---:|---:|
| Logger access Legacy | 43.90 ns/op | 40.51 ns/op | 24.68 M ops/s | 3.35% | 3.35% |
| Logger access Cached | 7.46 ns/op | 6.89 ns/op | 145.19 M ops/s | 1.38% | 1.32% |
| Level filtered Legacy | 54.54 ns/op | 50.29 ns/op | 19.88 M ops/s | 3.56% | 3.40% |
| Level filtered Cached | 16.15 ns/op | 14.91 ns/op | 67.09 M ops/s | 1.48% | 1.48% |

##### 5.1.6.1 Logger access 的直接收益

CPU 单次成本差值：

```text
40.51 - 6.89 = 33.62 ns/op
```

相对降低：

```text
(40.51 - 6.89) / 40.51 = 83.0%
```

加速比和吞吐提升：

```text
40.51 / 6.89 = 5.88x
145.19 / 24.68 = 5.88x
```

耗时加速比与吞吐加速比一致，说明 counter 和时间口径没有相互矛盾。Cached 并非零成本，剩余约 `6.89 ns/op` 包含 helper 分支、provider 函数、函数内 static 访问和 `shared_ptr` 引用计数。

##### 5.1.6.2 完整级别过滤路径的收益

CPU 单次成本差值：

```text
50.29 - 14.91 = 35.38 ns/op
```

相对降低和加速比：

```text
(50.29 - 14.91) / 50.29 = 70.4%
50.29 / 14.91 = 3.37x
```

吞吐从 `19.88 M ops/s` 提升到 `67.09 M ops/s`，约为 `3.37x`。完整过滤路径还包含 `Logger::shouldLog()`，因此 cached lookup 在总成本中的占比会低于纯 access 用例，`70.4%` 的相对改善小于纯 access 的 `83.0%` 是合理结果。

四个用例的 `Real median / CPU median` 都约为 `1.08`：

```text
Access Legacy  : 43.90 / 40.51 = 1.084
Access Cached  :  7.46 /  6.89 = 1.083
Filtered Legacy: 54.54 / 50.29 = 1.085
Filtered Cached: 16.15 / 14.91 = 1.083
```

该比例在 Legacy/Cached 和两个测试层次中基本一致，没有呈现只在 Legacy 路径放大的 wall-clock 等待。单线程下的 `loggers_mtx_` 主要表现为用户态无竞争锁和查找的 CPU 成本；是否在多线程下进入 futex 等待，仍需后续实验确认。

##### 5.1.6.3 用两层用例检查因果关系

两组 A/B 的绝对节省分别为：

```text
纯 Logger access 节省 : 33.62 ns/op
完整级别过滤节省     : 35.38 ns/op
```

二者只相差约 `1.76 ns/op`。考虑 Legacy CV 约 `3.4%`、不同函数布局以及宏控制流差异，这个差异处于可接受范围。它支持以下解释：

```text
LevelFiltered 的主要改善确实来自 Logger 获取路径，
而不是 shouldLog、LogAttr、formatter 或 I/O 的变化。
```

由两层用例还可以估算 lookup 之外的过滤成本：

```text
Legacy: 50.29 - 40.51 = 9.78 ns/op
Cached: 14.91 -  6.89 = 8.02 ns/op
```

这只是两个独立 benchmark 的差值估算，不能当成 `shouldLog()` 的精确函数耗时；但它说明移除 lookup 后，过滤判断本身进入约 8 到 10 ns 的量级。

##### 5.1.6.4 稳定性和验收门槛

Legacy access 的 CPU CV 为 `3.35%`，按当前验收公式：

```text
max(5%, 2 x 3.35%) = 6.70%
```

实际 CPU median 改善约 `83.0%`，显著超过 `6.70%`。

Legacy filtered 的 CPU CV 为 `3.40%`：

```text
max(5%, 2 x 3.40%) = 6.80%
```

实际改善约 `70.4%`，也显著超过 `6.80%`。四个用例的 CV 均低于 `5%`，其中 Cached 两组约 `1.3%` 到 `1.5%`，本轮数据足以建立单线程基线。

#### 5.1.7 perf stat 固定次数验证

Google Benchmark 证明了“快多少”，下一步通过固定一亿次操作回答“少执行了多少 CPU 工作”。两条路径分别执行：

```bash
sudo "$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- taskset -c 2 ./bin/log_benchmark \
        --benchmark_filter='BM_ModuleLoggerAccessLegacyPerfStat' \
        >/dev/null
```

以及：

```bash
sudo "$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- taskset -c 2 ./bin/log_benchmark \
        --benchmark_filter='BM_ModuleLoggerAccessCachedPerfStat' \
        >/dev/null
```

`-r 3` 会完整启动进程三次；perf 汇总行中的计数是三次运行的平均值，并在括号中给出波动。每次运行都由 benchmark 固定为 100,000,000 次，因此平均计数可以除以 `1e8` 得到每次操作成本，不能再除以重复次数 3。

原始主要计数和派生值：

| 指标 | Legacy | Cached | 相对变化 |
|---|---:|---:|---:|
| task-clock | 4371.11 ms | 677.83 ms | 降低 84.5% |
| task-clock/op | 43.711 ns | 6.778 ns | 降低 84.5% |
| cycles | 15,255,322,633 | 2,319,171,493 | 降低 84.8% |
| cycles/op | 152.55 | 23.19 | 降低 84.8% |
| instructions | 39,418,472,479 | 8,814,280,875 | 降低 77.6% |
| instructions/op | 394.18 | 88.14 | 降低 77.6% |
| branches | 8,403,501,014 | 1,602,658,512 | 降低 80.9% |
| branches/op | 84.04 | 16.03 | 降低 80.9% |
| branch misses | 1,281,007 | 363,620 | 绝对数量降低 71.6%，但 Cached 波动很高 |
| perf 报告 IPC | 2.72 | 3.78 | Cached 每周期完成更多指令 |
| cache references | 7,938,593 | 1,439,987 | 绝对数量降低 81.9% |
| cache misses | 1,231,892 | 309,104 | 绝对数量降低 74.9% |
| context switches | 33 | 4 | 都属于很低的进程级计数 |
| CPU migrations | 1 | 0 | `taskset` 有效抑制了迁移 |
| page faults | 298 | 298 | 与每次 Logger 获取无关的启动成本 |

这些指标分别说明：

```text
task-clock/op 降低：进程为一次 Logger 获取实际使用的 CPU 时间明显下降。
cycles/op 降低：收益不仅来自频率变化，而是执行周期真实减少。
instructions/op 降低：cached 路径删除了大量查找和锁相关指令。
branches/op 降低：unordered_map、字符串比较和锁控制流被移出热路径。
IPC 提升：剩余 cached 路径更短、更规则，CPU 流水线利用率更好。
```

`perf stat` 运行期间 Legacy 报告约 3.659 GHz，Cached 约 3.339 GHz。Cached 在较低报告频率下仍然显著减少 task-clock、cycles 和 instructions，因此不能把收益解释为单纯睿频差异。

cache miss 百分比不能单独用于判定退化：Cached 的 miss rate 百分比更高，是因为总 cache reference 降得更快；其 cache miss 绝对数从约 123 万降至约 31 万。对于固定操作次数，应优先比较绝对事件数或 events/op。

两边报告的 branch miss rate 都约为 `0.02%`，说明该短循环的分支高度可预测。Cached 的 branch-misses 重复波动达到约 `50%`，所以不能用这一事件的精确降低比例做主要结论；cycles、instructions 和 branches 的变化更稳定，也与源码路径相符。

上下文切换、CPU migration 和 page fault 不按每次 Logger 获取解释：

```text
Legacy context switches : 33
Cached context switches : 4
两边 page faults        : 298
```

这些数值很小，并且 page fault 完全相同，主要反映进程启动、动态库加载和 benchmark 框架行为。单线程 `perf stat` 没有提供严重调度等待的证据，也不能据此推断多线程下没有锁竞争。

Google Benchmark access CPU median 的加速比约 `5.88x`，perf 固定次数 task-clock 的加速比约 `6.45x`。两者不是同一次运行，且 perf 会引入计数开销、CPU 频率也不同，所以不要求数值完全一致；二者都指向约 80% 以上的单次 CPU 成本下降，结论一致。

#### 5.1.8 perf record/report 热点验证

`perf stat` 只能显示总工作量下降，不能告诉我们消失的是哪段代码。因此继续分别采集两条固定次数路径：

```bash
"$PERF_BIN" record \
    -F 999 \
    -g \
    --call-graph fp \
    -o tests/benchmarks/log/result/module-logger-single-access-legacy.perf.data \
    -- taskset -c 2 ./bin/log_benchmark \
        --benchmark_filter='BM_ModuleLoggerAccessLegacyPerfStat' \
        >/dev/null
```

Cached 使用相同参数，只替换 benchmark filter 和输出文件。两份文本报告按以下形式生成：

```bash
"$PERF_BIN" report \
    --stdio \
    --stdio-color=never \
    --percent-limit=0.5 \
    -i tests/benchmarks/log/result/module-logger-single-access-legacy.perf.data \
    > tests/benchmarks/log/result/module-logger-single-access-legacy.perf.report.txt
```

Cached 报告同样只替换文件名中的 `legacy` 为 `cached`。当前报告保留 `Children` 和 `Self` 两列以及调用树，用于同时观察父函数累计热点和函数自身热点。

采样完整性：

| 路径 | Samples | Event count | Lost Samples |
|---|---:|---:|---:|
| Legacy | 约 4K | 14,859,869,276 cycles | 0 |
| Cached | 702 | 2,287,725,933 cycles | 0 |

Cached 运行更短，因此在相同 999 Hz 采样频率下样本更少，这是优化后的正常结果。不能因为 Cached 只有 702 个样本就把它与 Legacy 的百分比直接相减。

Legacy 调用树的主要 children/self 样本：

| 符号 | 样本占比 | 解释 |
|---|---:|---|
| `LogManager::getLogger` | 73.33% children，6.80% self | 每轮进入 LogManager 查找路径 |
| `findOrCreateLoggerUnLocked` | 49.68% children，24.24% self | 已存在 Logger 仍需执行容器查找 |
| `__memcmp_avx2_movbe` | 17.44% self | key 比较最终落到内存比较 |
| `unordered_map::_M_find_before_node` | 13.08% self | hash bucket/node 查找 |
| `std::_Hash_bytes` | 8.67% self | 字符串 hash |
| `std::string::compare` | 6.99% self | Logger 名称比较 |
| `pthread_mutex_lock` | 5.23% self | `loggers_mtx_` 无竞争锁快路径也有 CPU 成本 |
| `pthread_mutex_unlock` | 4.13% self | 对应解锁成本 |
| `LogManager::GetInstance` | 3.20% self | 单例访问成本 |

`Children` 已经包含子调用，不能把同一调用链上的 `getLogger`、`findOrCreateLoggerUnLocked`、hash 和 memcmp 百分比相加。上述表格用于说明样本分布和调用关系，不用于重新计算“总占比”。

Cached 调用树只剩：

| 符号 | 样本占比 | 解释 |
|---|---:|---|
| `GetLoggerHelper` | 72.92% children，34.88% self | 模块名分支和 provider 调用 |
| `GetBaseLogger` | 42.34% self | 返回函数内 static `Logger::Ptr`，包含 shared_ptr 拷贝成本 |
| benchmark 本体及未解析小地址 | 少量 | 循环、优化屏障和局部指令 |

Cached 报告中没有出现：

```text
LogManager::getLogger
findOrCreateLoggerUnLocked
unordered_map 节点查找
std::_Hash_bytes
pthread_mutex_lock
pthread_mutex_unlock
```

这正是本轮最重要的函数级证据：函数内 static 初始化完成后，cached 热路径确实绕过了 `LogManager`、hash/map 和 `loggers_mtx_`，而不只是让相同路径偶然运行得更快。

`GetBaseLogger` 的 `42.34%` 是 Cached 运行内部的相对样本占比，不表示它比 Legacy 多消耗了 42.34% 的绝对 CPU。Cached 总 cycles 只有 Legacy 的约 15.2%，优化掉主要热点后，剩余短路径自然会占据更高的相对比例。

报告末尾：

```text
Cannot load tips.txt file, please install perf!
```

只是当前 perf 安装缺少可选 tips 文本，不影响采样、符号解析或报告结果。本轮两份报告均为 `Lost Samples: 0`。

#### 5.1.9 三层证据的统一结论

本轮证据链可以整理为：

```text
源码假设
    每次 KIT_LOGGER 都重复进入 LogManager、map 和 mutex
        |
        v
Google Benchmark
    access CPU 40.51 -> 6.89 ns/op，降低 83.0%
    filtered CPU 50.29 -> 14.91 ns/op，降低 70.4%
        |
        v
perf stat
    cycles/op       152.55 -> 23.19，降低 84.8%
    instructions/op 394.18 -> 88.14，降低 77.6%
    branches/op      84.04 -> 16.03，降低 80.9%
        |
        v
perf report
    Legacy 的 map/hash/string/mutex 热点在 Cached 中消失
```

三种工具从平均延迟、CPU 总工作量和函数热点三个角度得到一致结论，因此可以确认：

```text
文档 4.6 指出的模块 Logger 重复查找是当前关闭级别日志路径中的显著成本；
函数内 static Logger::Ptr 缓存能有效移除这段稳态热路径；
收益远高于当前测量噪声和验收阈值。
```

锁的结论必须限定为：

```text
已证明：单线程稳态 cached 路径不再执行 loggers_mtx_ lock/unlock。
尚未证明：Legacy 在多线程下的 mutex 等待时长和 futex 竞争程度。
不应执行：从 LogManager 全局删除 loggers_mtx_。
```

#### 5.1.10 本轮边界、遗留项与下一步

本轮结果还不能外推到整个日志系统：

| 边界 | 影响 |
|---|---|
| 只测试 `base` | 它是 helper 第一分支，不能代表靠后 provider 的分发成本 |
| 只测试 1 线程 | 证明了无竞争锁和查找的 CPU 成本，未证明多线程扩展性 |
| 只测试 access/filtered | 不代表格式化、Appender、文件写入和 flush 的端到端收益 |
| 使用 page cache/WSL2 | 后续文件结果不能宣称是物理持久化延迟 |
| cached 返回 `shared_ptr` 值 | 仍有引用计数成本，当前先保证所有权语义，不提前改为裸指针或引用 |
| Logger 对象身份缓存 | 当前按既定假设暂不处理运行期间替换同名 Logger 的热更新问题 |
| A/B 显式调用两条路径 | 正式合入前仍需验证宏为 0 和 1 的两个生产构建 |

本章没有记录日志专项功能测试的具体输出，因此在生产宏默认值或日志实现正式变更前，还必须补录 `test_log` 结果，验证格式、级别过滤、累计 flush 和析构落盘语义没有改变。

原计划的多线程扩展性实验已在 5.2 中完成；后续实际派发路径实验仍单独安排，不与本节模块缓存结果混合：

```text
Logger::logUnchecked / Appender snapshot -> 独立 dispatch benchmark
    -> 比较快照复制和锁外 append 的成本
    -> 不与 shouldLog 过滤路径混合
```

多线程实验不能使用 `taskset -c 2`，否则所有线程会被限制到一个逻辑 CPU，测到的是时间片竞争而不是真实扩展性。

---

### 5.2 `shouldLog()` 锁竞争定位与原子快速路径 A/B

5.1 解决了每次日志宏重复进入 `LogManager` 查找和 `loggers_mtx_` 的问题。完成模块 Logger 缓存后，`LevelFilteredCached` 的下一个热点变成 `Logger::shouldLog()`：即使 Logger 已缓存，每次级别过滤仍会获取 Logger 自身的 `appenders_mtx_`。本节只测量并优化这个锁竞争问题。

#### 5.2.1 问题假设与实验边界

旧实现的稳态路径为：

```text
Logger::shouldLog(level)
    -> appenders_mtx_.lock()
    -> 读取 output_route_、root_fallback_ 或 logger level
    -> appenders_mtx_.unlock()
    -> 返回过滤结果
```

在多线程同时过滤同一模块 Logger 的 DEBUG 日志时，实际并不需要访问 `appenders_` 容器；旧实现仍然让所有线程争用同一把锁。本轮固定 `MUDUO_LOG_CACHE_MODULE_LOGGER=1`，只切换 `MUDUO_LOG_SHOULDLOG_OPTIMIZE=0/1`，避免把 5.1 的 Logger map 查找变量混入结果。

被测 Logger 级别为 `ERROR`，日志级别为 `DEBUG`：

```text
DEBUG < ERROR
    -> 宏中的 shouldLog() 返回 false
    -> 不构造 LogAttr
    -> 不进入 formatter、Appender、文件 I/O 或 logUnchecked()
```

因此本节结论只适用于级别过滤热路径，不代表完整日志输出链路。

#### 5.2.2 生产代码优化方案

输出路由状态改为原子变量：

```cpp
std::atomic<OutputRoute> output_route_{OutputRoute::kRootFallback};
```

新 `shouldLog()` 先读取 route：

```text
route == kMuted       -> 直接返回 false
route == kOwnAppenders -> 读取 logger level 并比较
route == kRootFallback -> 进入带锁的 root fallback 慢路径
```

因此 `kOwnAppenders` 和 `kMuted` 快速路径不再获取 `appenders_mtx_`；只有 root fallback 需要访问受锁保护的 `root_fallback_`，才进入 `shouldLogWithRootFallback()`。慢路径在加锁后再次读取 route，避免锁外判断后配置切换导致错误读取。

配置写入侧先在 `appenders_mtx_` 内修改 `appenders_` / `root_fallback_`，最后使用 release store 发布 route。读侧使用 acquire load，从而保证 route 发布后能看到匹配的配置状态。

本轮还区分了安全入口和无重复过滤的派发入口：

```text
日志宏 -> shouldLog() -> 构造 LogAttr -> LogAttrWrap::~LogAttrWrap() -> logUnchecked()
public Logger::log() -> shouldLog() -> logUnchecked()
root fallback -> root->logUnchecked()
```

`logUnchecked()` 为 private，仅由 `LogAttrWrap` 作为 friend 调用；正常宏路径只过滤一次，直接调用 public `log()` 仍保留安全检查。

#### 5.2.3 宏隔离与串行构建流程

正式 A/B 固定：

```text
宏=0：-DMUDUO_LOG_CACHE_MODULE_LOGGER=1
      -DMUDUO_LOG_SHOULDLOG_OPTIMIZE=0

宏=1：-DMUDUO_LOG_CACHE_MODULE_LOGGER=1
      -DMUDUO_LOG_SHOULDLOG_OPTIMIZE=1
```

采用串行流程即可：配置/构建宏=0，运行并保存结果，再重新配置/构建宏=1。切换配置后必须检查 `compile_commands.json`，确认 `src/base/log.cpp` 和 benchmark 源码都带有期望的宏值。只给 benchmark 目标传宏无效，因为 `Logger::shouldLog()` 已经编译进共享库。

宏=0构建：

```bash
cmake -S . -B build-perf \
    -DCMAKE_BUILD_TYPE=Release \
    -DMUDUO_BENCH.LOG=ON \
    -DMUDUO_LOG_CACHE_MODULE_LOGGER=ON \
    -DMUDUO_LOG_SHOULDLOG_OPTIMIZE=OFF
cmake --build build-perf --target log_benchmark -j4
```

宏=1只把最后一个选项切换为 `ON`，其余配置保持不变：

```bash
cmake -S . -B build-perf \
    -DCMAKE_BUILD_TYPE=Release \
    -DMUDUO_BENCH.LOG=ON \
    -DMUDUO_LOG_CACHE_MODULE_LOGGER=ON \
    -DMUDUO_LOG_SHOULDLOG_OPTIMIZE=ON
cmake --build build-perf --target log_benchmark -j4
```

宏=0完成 perf record 后，应立即生成文本报告，或在重新编译前保存 `bin/log_benchmark` 和 `lib/libkit_muduo.so`。否则宏=1构建会覆盖旧符号，旧 `.perf.data` 的 report/annotate 可能无法准确解析。

#### 5.2.4 Google Benchmark 设计与控制条件

使用多线程过滤族：

```text
BM_ModuleLevelFilteredCached/real_time/threads:1
BM_ModuleLevelFilteredCached/real_time/threads:2
BM_ModuleLevelFilteredCached/real_time/threads:4
BM_ModuleLevelFilteredCached/real_time/threads:8
```

固定 PerfStat 实例：

```text
BM_ModuleLevelFilteredCachedPerfStat8
    -> Threads(8)
    -> Iterations(1,562,500 per worker)
```

Google Benchmark 显示总迭代数 `12,500,000`，乘以 8 个线程后实际工作量约 `100,000,000` 次过滤。两套构建使用相同 CPU 亲和性、最小测量时间、重复次数、过滤器和 JSON 格式；多线程使用 `UseRealTime()`，吞吐按 `state.iterations() * state.threads()` 计算。

多线程聚合数据使用以下参数采集，宏=0/1只替换输出文件名：

```bash
taskset -c 0-11 ./bin/log_benchmark \
    --benchmark_filter='^BM_ModuleLevelFilteredCached/real_time/threads:(1|2|4|8)$' \
    --benchmark_min_time=3.0 \
    --benchmark_repetitions=11 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=tests/benchmarks/log/result/module-level-filtered-ab-multithread-should.json \
    --benchmark_out_format=json
```

执行前使用 `--benchmark_list_tests` 核对注册名称。多线程测试绑定 `0-11`，不能绑定单个 CPU；否则测到的是时间片竞争，不是 `shouldLog()` 的真实扩展性。

#### 5.2.5 Google Benchmark A/B 结果

报告文件：

```text
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should.json
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should-optimize.json
```

聚合 mean：

| 线程数 | 宏=0 real time | 宏=1 real time | 加速比 | 宏=0 吞吐 | 宏=1 吞吐 |
|---:|---:|---:|---:|---:|---:|
| 1 | 33.83 ns | 17.79 ns | 1.90x | 29.59 M/s | 56.25 M/s |
| 2 | 78.62 ns | 45.86 ns | 1.71x | 25.46 M/s | 43.66 M/s |
| 4 | 106.74 ns | 59.90 ns | 1.78x | 37.50 M/s | 66.79 M/s |
| 8 | 173.09 ns | 65.84 ns | 2.63x | 46.23 M/s | 121.52 M/s |

固定 8 线程 PerfStat：

```text
宏=0：179.93 ns，44.51 M/s
宏=1： 65.68 ns，121.82 M/s
```

固定工作量下 real time 降低约 `63.5%`，吞吐提升约 `2.74x`。单线程约 `1.90x` 说明旧实现即使无竞争也支付 mutex lock/unlock 成本；8 线程约 `2.6` 至 `2.7x` 说明新实现同时消除了无竞争锁成本和共享锁竞争。

#### 5.2.6 perf report：锁热点消失

报告文件：

```text
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should.perf.report.txt
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should-optimize.perf.report.txt
```

宏=0主要热点：

```text
pthread_mutex_lock       28.37%
pthread_mutex_unlock     17.09%
__GI___lll_lock_wait     10.87%
futex lock/wake           约 2.48%
```

显式 mutex lock、unlock、wait 合计约 `56.33%`，证明 `appenders_mtx_` 是旧路径主要瓶颈。宏=1主要热点变为 `GetBaseLogger`（50.69%）和 benchmark 本体（48.82%），锁函数不再是主要采样热点；优化后的 `shouldLog()` 被内联到短路径，剩余成本主要是 static provider、原子 route/level 读取、分支和 benchmark 循环。

报告末尾 `Cannot load tips.txt file, please install perf!` 只是缺少可选 tips 文本，不影响采样或符号解析；两份报告的 `Lost Samples` 均为 0。

采样命令固定为 8 线程 PerfStat 实例：

```bash
"$PERF_BIN" record \
    -F 999 \
    -g \
    --call-graph fp \
    -o tests/benchmarks/log/result/module-level-filtered-ab-multithread-should.perf.data \
    -- taskset -c 0-11 ./bin/log_benchmark \
        --benchmark_filter='^BM_ModuleLevelFilteredCachedPerfStat8/iterations:1562500/real_time/threads:8$' \
        >/dev/null
```

宏=1只替换输出文件名中的 `should` 为 `should-optimize`。本轮最终锁热点结论来自 perf report；此前 bpftrace futex 数据用于发现锁竞争方向，但没有作为本轮宏=0/1的直接 A/B 数字。

#### 5.2.7 perf stat：CPU 工作量变化

报告文件：

```text
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should.perf.stat
tests/benchmarks/log/result/module-level-filtered-ab-multithread-should-optimize.perf.stat
```

| 指标 | 宏=0 | 宏=1 | 相对变化 |
|---|---:|---:|---:|
| task-clock | 13,463.90 ms | 5,207.80 ms | 降低 61.3% |
| instructions | 2.178 B | 0.907 B | 降低 58.3% |
| branches | 564.295 M | 251.219 M | 降低 55.5% |
| branch-misses | 23.448 M | 2.076 M | 降低 91.2% |
| cache-references | 153.113 M | 99.052 M | 降低 35.3% |
| cache-misses | 639,925 | 224,297 | 降低 65.0% |
| elapsed | 2.060 s | 0.645 s | 降低 68.7% |

归一化到约 `1e8` 次操作：

```text
宏=0：instructions/op 约 21.78，branches/op 约 5.64
宏=1：instructions/op 约  9.07，branches/op 约 2.51
```

这些事件支持“代码路径缩短”。cache-misses 应优先比较绝对数或 events/op，而不是单看 miss rate。

本轮 raw cycles 和 IPC 不作为主要结论：宏=0平均频率约 `0.434 GHz`，宏=1约 `2.884 GHz`，CPU 利用率也不同（`6.536` 对 `8.078`）。WSL2 的调度和频率状态差异使宏=1虽然 task-clock、instructions、elapsed 更低，raw cycles 反而更高，IPC 也不具备稳定可比性。正式结论使用 task-clock、instructions、branches、elapsed 和 perf report。

stat 使用相同固定实例和事件集合，外层 `-r 3` 完整启动进程三次：

```bash
sudo "$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- taskset -c 0-11 ./bin/log_benchmark \
        --benchmark_filter='BM_ModuleLevelFilteredCachedPerfStat8' \
        >/dev/null
```

报告中的计数是三次运行的平均值，每次仍对应约 `1e8` 次操作；归一化时除以 `1e8`，不能再除以重复次数 3。

#### 5.2.8 证据链与最终结论

```text
源码分析：shouldLog 每次获取 appenders_mtx_
    -> 宏隔离：固定 CACHE=1，只切换 SHOULDLOG_OPTIMIZE
    -> Google Benchmark：8线程吞吐约提升 2.6~2.7x
    -> perf report：旧版约 56% mutex/futex 样本，新版锁热点消失
    -> perf stat：task-clock -61%、instructions -58%、elapsed -69%
```

因此可以确认：`MUDUO_LOG_SHOULDLOG_OPTIMIZE=1` 成功移除了 `shouldLog()` 的 `kOwnAppenders` / `kMuted` 快速路径中的 `appenders_mtx_` lock/unlock 成本。该结论针对级别过滤热路径，不代表 formatter、Appender、文件写入或 flush 的端到端收益。

#### 5.2.9 当前边界与后续实验

| 边界 | 说明 |
|---|---|
| root fallback | 仍走带锁慢路径；本轮收益主要来自 `kOwnAppenders` 和 `kMuted` |
| 配置热更新 | route 使用 release/acquire；保证发布和内存安全，不承诺在途日志立即切换配置 |
| Logger 身份缓存 | 按既定假设暂不处理运行期间替换同名 Logger 的 static 缓存身份问题 |
| 日志派发 | 尚未测量 `logUnchecked()`、Appender 快照、formatter、I/O；应作为独立 dispatch benchmark |
| WSL2 计数 | CPU 频率差异使 raw cycles/IPC 不稳定；不据此宣称收益或退化 |

下一项实验应单独测量 `logUnchecked()` 的 `appender_snapshot` 复制和锁外 `LogAppender::append()`，不能与本节 `shouldLog()` A/B 混合解释。

## 6. Google Benchmark 参数与输出字段详解

前面的执行命令可以拆成四类参数：选择用例、控制测量时间、控制重复次数、控制结果输出。

```bash
./bin/log_benchmark \
    --benchmark_filter='BM_(BaselineLoop|LevelFiltered)$' \
    --benchmark_min_time=0.5 \
    --benchmark_repetitions=7 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/kit-log-bench/level-filtered.json \
    --benchmark_out_format=json
```

### 6.1 `--benchmark_filter`

格式：

```text
--benchmark_filter='<regular-expression>'
```

它使用正则表达式匹配注册名称，不是普通字符串比较。

```bash
# 只匹配一个完整名称
--benchmark_filter='^BM_LevelFiltered$'

# 匹配所有包含该片段的名称
--benchmark_filter='BM_LevelFiltered'

# 同时匹配两个完整名称
--benchmark_filter='^(BM_BaselineLoop|BM_LevelFiltered)$'
```

没有 `^` 和 `$` 时，`BM_LevelFiltered` 会同时匹配：

```text
BM_LevelFiltered
BM_LevelFilteredCached
BM_LevelFilteredPerfStat
BM_LevelFilteredCachedPerfStat
```

编写过滤器前先查看真实注册名称：

```bash
./bin/log_benchmark --benchmark_list_tests
```

`--benchmark_list_tests` 只列出用例，不执行测量。

### 6.2 `--benchmark_min_time`

```bash
--benchmark_min_time=0.5
```

表示每个 benchmark 实例至少测量约 0.5 秒。它不是“只执行一次并等待 0.5 秒”。Google Benchmark 会自动校准：

```text
少量迭代试跑
    -> 估算单次耗时
    -> 增加迭代次数
    -> 累计时间达到 min_time
    -> 报告累计时间 / 迭代次数
```

因此输出的 `70 ns` 是大量调用摊销后的平均值，不是只调用一次的秒表延迟。

自动校准适合平均耗时和吞吐测试。perf 的 `cycles/op` 比较则使用当前代码中的固定实例：

```cpp
BENCHMARK(BM_LevelFiltered)
    ->Name("BM_LevelFilteredPerfStat")
    ->Iterations(100'000'000);
```

固定迭代后可以计算：

```text
cycles/op = total cycles / 100000000
instructions/op = total instructions / 100000000
```

不要把自动校准用例和固定一亿次用例的 perf 总 cycles 直接比较。

### 6.3 `--benchmark_repetitions`

```bash
--benchmark_repetitions=7
```

表示同一个 benchmark 独立运行 7 轮。每轮内部仍然可能执行数百万或一亿次操作。

7 轮结束后生成：

| 后缀 | 含义 |
|---|---|
| `_mean` | 算术平均值 |
| `_median` | 中位数，优化前后优先比较 |
| `_stddev` | 标准差，表示绝对波动 |
| `_cv` | 变异系数，即 `stddev / mean` |

### 6.4 `--benchmark_report_aggregates_only`

```bash
--benchmark_report_aggregates_only=true
```

该参数只隐藏 7 轮原始结果，仍然执行全部 7 轮。

聚合行中的：

```text
Iterations = 7
```

表示参与聚合的重复轮数，不表示日志接口只执行了 7 次。查看实际单轮迭代次数时，去掉该参数，或使用：

```bash
--benchmark_repetitions=1
```

### 6.5 JSON 输出参数

```bash
--benchmark_out=/tmp/kit-log-bench/result.json
--benchmark_out_format=json
```

`benchmark_out` 写结果文件，但不会自动关闭控制台输出。`>/dev/null` 只丢弃目标程序 stdout；perf 默认写 stderr，所以外层 perf 统计仍可显示。

典型 JSON：

```json
{
  "name": "BM_LevelFiltered_median",
  "run_type": "aggregate",
  "repetitions": 7,
  "threads": 1,
  "iterations": 7,
  "real_time": 70.8798,
  "cpu_time": 65.4247,
  "time_unit": "ns",
  "items_per_second": 1.5284e+07
}
```

| 字段 | 含义 |
|---|---|
| `name` | 原始或聚合名称 |
| `run_type` | `aggregate` 表示聚合行 |
| `repetitions` | 聚合轮数 |
| `threads` | benchmark 线程数 |
| `iterations` | 原始行是实际迭代次数；聚合行通常是重复轮数 |
| `real_time` | 每次迭代 wall-clock 时间 |
| `cpu_time` | 每次迭代 CPU 时间 |
| `time_unit` | `real_time/cpu_time` 的单位 |
| `items_per_second` | `SetItemsProcessed` 计数除以测量时间 |

JSON 中 CV 使用小数：

```json
"real_time": 3.9672368608672397e-02
```

对应：

```text
0.039672... = 3.9672%
```

JSON context 常见字段：

| 字段 | 解读 |
|---|---|
| `date` | benchmark 启动时间 |
| `host_name` | 机器名；不同机器不能直接比较绝对 ns |
| `num_cpus` | 逻辑 CPU 数量 |
| `mhz_per_cpu` | 系统报告频率，不等于运行时真实睿频 |
| `cpu_scaling_enabled` | 框架检测到的 scaling 状态，不等价于绝对锁频 |
| `load_avg` | 1、5、15 分钟系统负载 |
| `library_build_type` | 框架根据构建宏推断的 release/debug，不替代编译命令 |

### 6.6 `Time`、`CPU` 和 `items_per_second`

`Time` 是 wall-clock，包含 CPU 执行、被调度出去、等锁和阻塞 I/O。

`CPU` 是进程真正消耗的 CPU 时间。当前 benchmark 没有调用 `UseRealTime()`，rate counter 默认按 CPU 时间计算：

```text
1 / 65.4 ns ≈ 15.29 M/s
1 / 9.32 ns ≈ 107.30 M/s
```

这与当前输出的 `items_per_second` 一致。

同步文件日志需要重点看 `Time`，因为业务线程会感受到 I/O 等待；formatter、hash、字符串比较等纯计算成本需要重点看 `CPU`。

### 6.7 其他常用参数

先查看当前 Google Benchmark 1.6.1 实际支持的参数：

```bash
./bin/log_benchmark --help
```

| 参数 | 示例 | 作用 |
|---|---|---|
| `--benchmark_list_tests` | 无值 | 列出注册用例 |
| `--benchmark_filter` | `'^BM_.*$'` | 正则选择用例 |
| `--benchmark_min_time` | `1` | 每个实例至少运行约 1 秒 |
| `--benchmark_repetitions` | `10` | 重复测量 |
| `--benchmark_report_aggregates_only` | `=true` | 只显示聚合统计 |
| `--benchmark_out` | `/tmp/a.json` | 结果文件 |
| `--benchmark_out_format` | `json/csv` | 结果格式 |
| `--benchmark_time_unit` | `ns/us/ms` | 显示单位 |
| `--benchmark_color` | `false` | 关闭颜色，适合保存日志 |
| `--benchmark_counters_tabular` | `true` | counter 按列显示 |

---

## 7. 探索阶段 Benchmark 稳定性示例

本节保留正式 A/B 设计前的 `BM_LevelFiltered` 探索数据，用于说明 CV 和验收门槛的计算方法。由于当时使用的 logger 名称、接口边界和正式测试不同，这组绝对值不能与第 5 章的 `BM_Module*` 结果直接混用。

推荐判断标准：

```text
CV < 3%    非常稳定
CV < 5%    可以接受
CV 5~10%   应检查负载、调度和后台任务
CV > 10%   不应直接做优化结论
```

优化收益最低门槛：

```text
median 改善 > max(5%, 2 × 基线 CV)
```

探索阶段结果：

```text
BM_LevelFiltered_median
real_time: 70.9 ns
cpu_time : 65.4 ns
CV       : 3.56%

BM_LevelFilteredCached_median
real_time: 10.1 ns
cpu_time : 9.32 ns
CV       : 3.09%
```

差值：

```text
real_time: 70.9 - 10.1 = 60.8 ns
cpu_time : 65.4 - 9.32 = 56.08 ns
```

查找相关成本近似占：

```text
60.8 / 70.9 ≈ 85.8% real_time
56.08 / 65.4 ≈ 85.7% cpu_time
```

当时得到的初步假设：

```text
KIT_LOGGER()
    -> LogManager::getLogger()
    -> loggers_mtx_
    -> unordered_map hash/bucket
    -> string compare
```

比缓存 Logger 后剩余的 `shouldLog()` 路径更昂贵。该假设后来已通过第 5 章的正式 A/B、`perf stat` 和 `perf report` 完成交叉验证。

---

## 8. 构建和实验环境检查

推荐性能构建：

```bash
cmake -S . -B build-perf \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DMUDUO_BENCH.LOG=ON \
    -DMUDUO_TEST=OFF \
    -DWORK_TEST=OFF
```

只编译目标：

```bash
cmake --build build-perf --target log_benchmark -j4
```

检查 benchmark 和日志库：

```bash
rg -n -m1 'log_benchmark.cpp' build-perf/compile_commands.json
rg -n -m1 '/src/base/log.cpp' build-perf/compile_commands.json
```

两者都应包含：

```text
-O2 或 -O3
-g
-DNDEBUG
-fno-omit-frame-pointer
```

不能包含 `-O0`。

`RelWithDebInfo` 的 JSON 可能显示 `"library_build_type": "release"`，因为 Google Benchmark 主要依据 `NDEBUG` 推断，不代表失去了 `-g`。以 `compile_commands.json` 为准。

每次实验至少记录：

```bash
date -Is
uname -a
lscpu | sed -n '1,35p'
cat /proc/sys/kernel/perf_event_paranoid
git rev-parse --short HEAD
git status --short
df -T /tmp "$PWD"
```

当前文件 benchmark 应使用 WSL ext4 的 `/tmp`，不要放到 `/mnt/c`。Windows 文件系统桥接会显著改变 I/O 结果。

单线程可固定 CPU：

```bash
taskset -c 0 ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFiltered$' \
    --benchmark_min_time=1
```

多线程不能全部固定到一个 CPU，否则测到的是时间片竞争而非真实扩展性。

---

## 9. perf stat：性能计数器

### 9.1 固定次数命令

普通 logger 查找：

```bash
"$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredPerfStat$' \
        >/dev/null
```

缓存 logger：

```bash
"$PERF_BIN" stat -r 3 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null
```

| 参数 | 含义 |
|---|---|
| `stat` | 统计整个被测进程 |
| `-r 3` | 完整启动目标进程 3 次并汇总 |
| `-e` | 指定事件列表 |
| `--` | perf 参数结束，后面是目标程序 |
| `>/dev/null` | 丢弃 benchmark stdout，保留 perf stderr |
| `PerfStat` 后缀 | 选择固定一亿次实例 |

`perf -r 3` 是外层完整进程重复；`benchmark_repetitions=7` 是框架内部重复。perf 固定次数实验通常让 Google Benchmark repetitions 保持 1，避免形成 `3 × 7`。

### 9.2 事件含义

| 事件 | 含义 | 主要用途 |
|---|---|---|
| `task-clock` | 进程实际占用 CPU 的时间 | 与 benchmark CPU time 对照 |
| `cycles` | CPU 周期 | CPU 总成本 |
| `instructions` | retired instructions | 指令数量 |
| `branches` | 分支数量 | hash/查找/条件分支规模 |
| `branch-misses` | 分支预测失败 | 分支可预测性 |
| `cache-references` | cache 参考计数 | 粗略 locality |
| `cache-misses` | cache 未命中 | cache 局部性 |
| `context-switches` | 调度切换次数 | 调度或阻塞 |
| `cpu-migrations` | CPU 迁移 | 亲和性噪声 |
| `page-faults` | 缺页 | 首次加载/映射干扰 |

常用派生指标：

```text
IPC = instructions / cycles
branch_miss_rate = branch-misses / branches
cache_miss_rate = cache-misses / cache-references
cycles/op = cycles / fixed_iterations
instructions/op = instructions / fixed_iterations
```

示例：

```text
cycles = 3,000,000,000
instructions = 2,400,000,000
iterations = 100,000,000

IPC = 0.80
cycles/op = 30
instructions/op = 24
```

### 9.3 perf stat 输出解读

典型输出：

```text
3,413,709,206      cycles:u
2,xxx,xxx,xxx      instructions:u  # 0.xx insn per cycle
      xxx,xxx      context-switches:u
            0      cpu-migrations:u
```

`cycles:u` 的 `:u` 表示 user mode，不是单位。

| 输出 | 含义 |
|---|---|
| `<not supported>` | 当前内核/虚拟化不支持该事件 |
| `<not counted>` | 没有有效计数，可能是权限或硬件计数器资源问题 |
| `enabled/running < 100%` | 发生 multiplex，事件不是全程同时计数 |
| `seconds time elapsed` | wall-clock，不等于 CPU time |
| `insn per cycle` | perf 自动计算的 IPC |

硬件事件失败时降级：

```bash
"$PERF_BIN" stat -r 3 \
    -e task-clock,cpu-clock,context-switches,cpu-migrations,page-faults \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredPerfStat$' \
        >/dev/null
```

### 9.4 常用但示例未出现的 stat 参数

```bash
# 常见详细事件
"$PERF_BIN" stat -d -r 3 -- ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredPerfStat$' >/dev/null

# 输出文件
"$PERF_BIN" stat -o /tmp/kit-log-bench/lookup.stat.txt \
    -e cycles,instructions -- ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredPerfStat$' >/dev/null

# CSV 风格，便于脚本处理
"$PERF_BIN" stat -x, \
    -e cycles,instructions -- ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredPerfStat$' >/dev/null

# 强制只统计用户态
"$PERF_BIN" stat --all-user \
    -e cycles,instructions -- ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredPerfStat$' >/dev/null
```

`-d/-dd/-ddd` 会逐步增加 cache/TLB 事件；事件过多会增加 multiplex 风险。

---

## 10. perf record：采样调用栈

普通 logger：

```bash
"$PERF_BIN" record \
    -F 999 \
    -g \
    --call-graph fp \
    -o /tmp/kit-log-bench/lookup.perf.data \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredPerfStat$' \
        >/dev/null
```

缓存 logger：

```bash
"$PERF_BIN" record \
    -F 999 \
    -g \
    --call-graph fp \
    -o /tmp/kit-log-bench/lookup-cached.perf.data \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null
```

| 参数 | 含义 |
|---|---|
| `record` | 采样并写 perf.data |
| `-F 999` | 约 999 Hz 采样 |
| `-g` | 记录调用栈 |
| `--call-graph fp` | 使用 frame pointer 展开 |
| `-o` | 输出数据文件 |
| `--` | perf 参数结束 |

`-F` 是采样频率，不是接口调用次数。频率越高，分辨率、开销和文件体积都越高。

调用栈方式：

| 方式 | 说明 |
|---|---|
| `fp` | 当前项目首选，需要 `-fno-omit-frame-pointer` |
| `dwarf` | 依赖 DWARF unwind，开销和体积更高 |
| `lbr` | Intel LBR，WSL/虚拟机可能不支持 |

也可使用事件周期：

```bash
"$PERF_BIN" record \
    -c 1000000 -e cycles \
    -g --call-graph fp \
    -o /tmp/kit-log-bench/lookup-cycle.data \
    -- ./bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredPerfStat$' \
        >/dev/null
```

`-F 999` 按时间频率采样；`-c 1000000` 每累计约一百万 cycles 采一个样本。优化前后应保持同一种方式。

---

## 11. perf report、annotate 和常用子命令

### 11.1 report

```bash
"$PERF_BIN" report \
    --stdio \
    --no-children \
    --percent-limit 0.5 \
    --sort=dso,symbol \
    -i /tmp/kit-log-bench/lookup-cached.perf.data \
    > tests/benchmarks/log/result/lookup-cached.perf.parsed.txt
```

| 参数 | 含义 |
|---|---|
| `--stdio` | 纯文本输出 |
| `--no-children` | 不把子函数 overhead 累加给父函数 |
| `--percent-limit 0.5` | 隐藏小于 0.5% 条目 |
| `--sort=dso,symbol` | 先共享对象，再符号 |
| `-i` | 输入 perf.data |

报告表头：

```text
Total Lost Samples: 0
Samples: 5K of event 'cycles:u'
Event count (approx.): 17695496469
```

| 字段 | 解读 |
|---|---|
| `Lost Samples` | perf 缓冲区丢失样本；0 理想 |
| `Samples` | 实际采样数量，不是调用次数 |
| `Event count` | 整段运行的事件总数估算 |
| `Overhead` | 该符号采样占比 |
| `Shared Object` | 符号所属 ELF/共享库 |
| `Symbol` | 函数符号 |

`Samples: 5K` 与 `Event count: 17.7B cycles` 不冲突：前者由采样频率决定，后者是事件总计数估算。

不要把同一调用链中父函数、hash、memcmp 的百分比简单相加。

### 11.2 探索阶段 LevelFiltered lookup 报告

本节与下一节记录的是早期 `BM_LevelFiltered`/`BM_LevelFilteredCached` 采样，用于学习如何从报告中区分 Logger lookup 和 `shouldLog()`。正式模块 Logger access 的两份报告和结论见第 5.8 节。

主要热点：

```text
findOrCreateLoggerUnLocked             22.15%
unordered_map::_M_find_before_node    10.63%
std::_Hash_bytes                      10.52%
memcmp                                10.01%
pthread_mutex_lock                     9.04%
pthread_mutex_unlock                   5.33%
LogManager::getLogger                  6.27%
Logger::shouldLog                      5.19%
```

证据链：

```text
getLogger
    -> loggers_mtx_
    -> hash
    -> bucket/node 查找
    -> string compare/memcmp
    -> 返回 Logger::Ptr
    -> shouldLog
```

### 11.3 探索阶段 LevelFiltered cached 报告

```text
Logger::shouldLog                    25.95%
pthread_mutex_lock                   23.27%
pthread_mutex_unlock                 15.06%
BM_LevelFilteredCached               25.66%
```

缓存 logger 后 map/hash/string compare 消失，剩余成本集中在：

```text
appenders_mtx_.lock
    -> output_route_ 判断
    -> atomic level load
    -> appenders_mtx_.unlock
```

### 11.4 annotate

```bash
"$PERF_BIN" annotate \
    --stdio \
    --symbol='kit_muduo::Logger::shouldLog' \
    -i /tmp/kit-log-bench/lookup-cached.perf.data \
    > tests/benchmarks/log/result/lookup-cached.shouldlog.annotate.txt
```

常用参数：

| 参数 | 作用 |
|---|---|
| `--stdio` | 文本输出 |
| `--symbol` | 只分析一个符号 |
| `--source` | 尽量显示源码 |
| `-M intel` | Intel 汇编语法 |

汇编左侧百分比是样本命中比例，不是精确调用次数。某行显示 0 只代表本次样本没有落到，不表示该路径永远不执行。

检查调试信息：

```bash
readelf -S lib/libkit_muduo.so | rg 'debug|symtab|strtab'
nm -D -C lib/libkit_muduo.so | rg 'Logger::shouldLog'
```

perf.data 和共享库必须来自同一次构建，否则 build-id/地址不匹配。

### 11.5 `perf top`、`script`、`diff`

实时热点：

```bash
./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
    >/dev/null &
TARGET_PID=$!

"$PERF_BIN" top -p "$TARGET_PID" -g
wait "$TARGET_PID"
```

`perf top` 适合快速观察，不适合最终可复现报告。

导出原始样本：

```bash
"$PERF_BIN" script \
    -i /tmp/kit-log-bench/lookup-cached.perf.data \
    > /tmp/kit-log-bench/lookup-cached.perf.script.txt
```

可用于 FlameGraph 或自定义栈统计。

比较优化前后：

```bash
"$PERF_BIN" diff \
    -i /tmp/kit-log-bench/before.data \
    -i /tmp/kit-log-bench/after.data
```

查看数据文件环境：

```bash
"$PERF_BIN" report --header-only \
    -i /tmp/kit-log-bench/lookup-cached.perf.data
```

### 11.6 `perf lock` 和 `perf sched`

`perf report` 看到 `pthread_mutex_lock` 只说明 CPU 样本落在锁路径，不能直接证明线程睡眠等待。主流的进一步手段是 `perf lock`、`perf sched` 和 bpftrace futex。

`perf lock` 记录内核锁事件。由于依赖 tracepoint 和内核权限，WSL2 中可能不可用；应将失败记录为环境限制，不要用 `sudo` 反复重试：

```bash
mkdir -p /tmp/kit-log-bench/perf-lock
cd /tmp/kit-log-bench/perf-lock

sudo "$PERF_BIN" lock record -- \
    /home/kewin/kit_muduo_cpp2/bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null

sudo "$PERF_BIN" lock report -i perf.data
```

常见报告字段：

| 字段 | 含义 |
|---|---|
| `acquired` | 成功获取锁次数 |
| `contended` | 发生竞争次数 |
| `avg wait` | 平均等待时间 |
| `max wait` | 最大等待时间 |
| `total wait` | 总等待时间 |

`perf lock` 主要面向内核锁，并不保证完整观察 glibc 用户态 mutex。无竞争的 pthread mutex 快路径不会进入内核，因此当前日志锁仍需要 bpftrace futex 和多线程 benchmark 交叉验证。

`perf sched` 记录调度事件：

```bash
mkdir -p /tmp/kit-log-bench/perf-sched
cd /tmp/kit-log-bench/perf-sched

sudo "$PERF_BIN" sched record -- \
    /home/kewin/kit_muduo_cpp2/bin/log_benchmark \
        --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
        >/dev/null

sudo "$PERF_BIN" sched timehist -i perf.data
```

`timehist` 常见字段：

| 字段 | 含义 |
|---|---|
| `time` | 调度事件时间戳 |
| `cpu` | 运行所在 CPU |
| `task name/pid` | 线程名称和 PID/TID |
| `wait time` | runnable 前的等待时间 |
| `sch delay` | 已经 runnable 但未获得 CPU 的时间 |
| `run time` | 本次连续运行时间 |

`perf sched` 适合解释 `real_time` 明显高于 `cpu_time` 的原因。若两者很接近，且 context switch 很少，优先分析用户态指令、hash 和锁快路径，而不是调度器。


---

## 12. bpftrace 0.14 使用边界

当前已验证：

```text
tracepoint: yes
kprobe: yes
perf_event: yes
uprobe refcount: yes
libdw: no
bfd: no
```

已知限制：

- `BEGIN` 出现过 `/proc/self/exe:BEGIN_trigger` 无法解析；
- `-c` 配合 `cpid` 曾过滤为 0；
- BTF 文件存在，但 0.14 的 BTF feature 可能显示 no；
- 用户栈源码解析有限，源码热点交给 perf。

使用 interval smoke：

```bash
sudo bpftrace -e '
interval:s:1
{
    printf("bpftrace smoke test: ok\n");
    exit();
}'
```

列探针：

```bash
sudo bpftrace -l 'tracepoint:syscalls:sys_enter_write'
sudo bpftrace -l 'tracepoint:syscalls:sys_enter_futex'
sudo bpftrace -l 'tracepoint:sched:sched_switch'
```

无过滤 write smoke：

```bash
sudo bpftrace -e '
tracepoint:syscalls:sys_enter_write
{
    @write_count = count();
    @write_bytes = sum(args->count);
}' -c '/usr/bin/printf bpftrace-smoke'
```

无过滤结果会包含其他进程。真实实验先获取 PID，再明确过滤：

```bash
TARGET_PID=12345

sudo bpftrace -e "
tracepoint:syscalls:sys_enter_write
/pid == $TARGET_PID/
{
    @write_count = count();
    @write_bytes = sum(args->count);
}
"
```

用户态 profile：

```bash
sudo bpftrace -e '
profile:hz:99
/comm == "log_benchmark"/
{
    @[ustack] = count();
}
interval:s:5
{
    print(@);
    exit();
}'
```

数字是采样次数，不是函数调用次数。

futex：

```bash
TARGET_PID=12345

sudo bpftrace -e "
tracepoint:syscalls:sys_enter_futex
/pid == $TARGET_PID/
{
    @futex_calls = count();
}
interval:s:5
{
    print(@futex_calls);
    exit();
}
"
```

futex 高说明锁竞争进入内核等待/唤醒。futex 为 0 不表示没有 mutex 成本，无竞争锁通常在用户态完成。

write 延迟：

```bash
TARGET_PID=12345

sudo bpftrace -e "
tracepoint:syscalls:sys_enter_write
/pid == $TARGET_PID/
{
    @start[tid] = nsecs;
}
tracepoint:syscalls:sys_exit_write
/pid == $TARGET_PID && @start[tid]/
{
    @write_latency = hist(nsecs - @start[tid]);
    delete(@start[tid]);
}
interval:s:5
{
    print(@write_latency);
    exit();
}
"
```

`hist()` 是 2 的幂次桶，适合观察长尾。stream buffer 可能合并多条日志，因此 write 次数不等于日志条数。

uprobe 前先找符号：

```bash
nm -D lib/libkit_muduo.so \
    | rg 'Logger.*shouldLog|Logger.*log|LogFormatter.*format|FileAppender.*log'

nm -D -C lib/libkit_muduo.so \
    | rg 'Logger::shouldLog|Logger::log|LogFormatter::format|FileAppender::log'
```

C++ uprobe 优先使用未 demangle 的符号。挂载 uprobe 会扰动耗时，因此只用于函数延迟分布验证，不作为正式 benchmark 成绩。

---

## 13. 其他常用辅助工具

CPU 亲和性：

```bash
taskset -pc $$
taskset -c 0 ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFiltered$' \
    --benchmark_min_time=1
```

syscall 汇总：

```bash
strace -f -c ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredCachedPerfStat$' \
    >/dev/null
```

`strace -c` 关注 `calls`、`errors`、`total time`、`usecs/call`。过滤路径不应出现大量 write/fsync/open。

JSON：

```bash
jq '.benchmarks[]
    | select(.aggregate_name == "median")
    | {name, real_time, cpu_time, time_unit, items_per_second}' \
    /tmp/kit-log-bench/lookup-vs-cached.json
```

没有 jq：

```bash
rg -n \
    '"name"|"real_time"|"cpu_time"|"time_unit"|"items_per_second"' \
    /tmp/kit-log-bench/lookup-vs-cached.json
```

超时保护：

```bash
timeout 30s ./bin/log_benchmark \
    --benchmark_filter='^BM_LevelFilteredPerfStat$'
```

返回码 124 表示被 timeout 终止，该次结果无效。

---

## 14. 优化前后的统一流程

每次只改一个因素，并保持：

```text
CMAKE_BUILD_TYPE
-O2/-O3
-g
-fno-omit-frame-pointer
benchmark filter
固定迭代次数
repetitions
perf event
采样方式
CPU 亲和性
文件系统
```

流程：

```text
记录 git status 和编译参数
    -> 运行 test_log
    -> Google Benchmark JSON
    -> perf stat
    -> perf record/report/annotate
    -> 修改一个因素
    -> 重复完全相同的命令
```

最低验收：

```text
目标 median 改善 > max(5%, 2 × 基线 CV)
CV 不明显恶化
test_log 通过
cycles/op 不反向恶化
instructions/op 不反向恶化
多线程扩展性没有新增明显回退
日志输出字节保持一致
```

原始 JSON、perf.data 和 script 建议放：

```text
/tmp/kit-log-bench/
```

仓库只保留经过筛选的文本报告和方法论。perf.data 依赖同一次构建的 build-id 和共享库。

当前结论：

```text
1. 普通过滤路径主要额外成本来自 LogManager::getLogger：
   loggers_mtx_、hash/bucket、字符串比较和 shared_ptr 返回。

2. 缓存 Logger 后主要剩余成本来自 Logger::shouldLog：
   appenders_mtx_ lock/unlock、路由判断和 atomic level load。

3. 尚未完成多线程和 futex 证据，不能只凭单线程 mutex 样本删除锁。
```

下一步实验：

```text
LoggerAccess Legacy/Cached -> Threads(1, 2, 4, 8)
LevelFiltered Legacy/Cached -> Threads(1, 2, 4, 8)
    -> 比较吞吐扩展性
    -> perf stat 比较 context-switch
    -> bpftrace 比较 futex
    -> 先验证模块 Logger 缓存的扩展性，再进入下一项优化
```
