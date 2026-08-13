# SQLite 历史存储基准测试汇总报告

> 报告日期：2026-07-24
> 测试范围：SQLite 增量 BLOB、SQLite 元数据 + 本地文件 raw 方案对照
> 数据来源：tests/benchmarks/results/ 下实际生成的 summary.json、samples.csv、environment.json 和 ZIP 导出结果
> schema 说明：本报告数据来自 2026-07-23/24 的旧 benchmark schema；2026-07-24 已将 benchmark 代码对齐到最终 interaction_payload_refs.ranges_json 等表设计并通过编译，尚未重跑基准。
> 结论性质：本报告用于当前内网业务的工程选型和容量规划，不替代真实设备流量、生产磁盘和正式上线前的稳定性验收。

## 1. 执行摘要

当前业务规模约为 200 个内网用户，每人平均 2-3 个 project，因此预计有 400-600 个 project；每个 project 含 5-6 个 protocol，预计有 2,000-3,600 个 project-protocol 配置关系。每个 project 至少包含一个约 2 MiB 的图片请求类型 protocol。

在本次相同输入、相同校验和相同 WAL 配置的测试中，推荐采用：

~~~text
SQLite 保存历史元数据和 payload locator
本地文件保存完整 request/response raw bytes
~~~

也就是 benchmark 中的 local_file 后端。

核心依据：

- 常规 hash-on profile 中，本地文件 raw 吞吐为 44.915-46.534 MiB/s，SQLite BLOB 为 20.726-22.596 MiB/s，约为 2.0-2.2x；
- 10 records/s、60 秒长时写入两种后端都稳定，队列没有持续增长；
- 50 records/s、60 秒压力轮两种后端都无法稳定吸收输入，SQLite 实际完成 23.465 records/s，本地文件完成 42.901 records/s；
- 2 MiB 以上的大对象会显著拉开事务成本：SQLite 的事务 p95 随对象大小增长，本地文件的 metadata 事务 p95 基本保持在 0.15-0.20 ms；
- 并发读取、ZIP 导出、清理和异常退出的最终正确性检查均通过；
- 本地文件方案的代价是需要正式实现 temp + fsync + rename、orphan raw 清理、导出保护和数据库/raw 一致性备份。

对于当前 200 人场景，真正的风险不在 project 或 protocol 的静态数量，而在于：

1. 同一时间有多少 project 产生交互；
2. 每个 project 多久执行一次完整 protocol 扫描；
3. 图片请求是否自动周期性产生；
4. 历史数据保留多少天；
5. 导出是否与写入高峰重叠。

如果图片请求只是用户手动触发或低频执行，当前 10 records/s 保守线通常有机会满足；如果每个 project 每分钟都执行一次 5-6 个 protocol，则中心估算为约 45.8 records/s，已经接近本次 50/s 失稳区间，必须通过业务调度、队列背压或多实例拆分控制峰值。

## 2. 测试范围和环境

### 2.1 两种候选方案

| 方案 | SQLite 中保存 | 文件系统中保存 | 主要特点 |
| --- | --- | --- | --- |
| SQLite BLOB | record、object、payload locator、完整 raw BLOB | 无 | 事务边界简单，单文件备份简单，但大 BLOB 进入 WAL 和写事务 |
| SQLite + local file | record、object metadata、payload locator、SHA-256、object key | 完整 raw bytes | 大对象 I/O 脱离 SQLite 事务，性能更好，但存在双存储一致性边界 |

### 2.2 测试环境

| 项目 | 实际值 |
| --- | --- |
| 操作系统 | WSL2，Linux 6.18.33.2-microsoft-standard-WSL2 |
| CPU | x86_64，12 CPU threads |
| 内存 | 约 15 GiB |
| 文件系统 | ext4 |
| SQLite | 3.50.2 |
| OpenSSL | 3.0.2，libssl-dev 3.0.2-0ubuntu1.25 |
| SQLite journal | WAL |
| busy_timeout | 3000 ms |
| 主测试 synchronous | NORMAL |
| chunk 对照 | 64 KiB、256 KiB、1 MiB |
| hash | SHA-256，使用 OpenSSL EVP；主结果为 hash-on |

OpenSSL 3.0.2 已满足当前基准和项目 EVP Crypto 依赖，本次测试没有发现需要升级 OpenSSL 的理由。

### 2.3 输入数据

单尺寸 profile 使用确定性数据，尺寸为：

~~~text
1 KiB, 16 KiB, 64 KiB, 256 KiB,
1 MiB, 8 MiB, 16 MiB, 64 MiB
~~~

常规 mixed profile 为：

~~~text
1 KiB, 16 KiB, 64 KiB, 256 KiB,
1 MiB, 1 MiB, 1 MiB, 8 MiB
~~~

8 个 mixed object 每轮 raw 总量为 11.329 MiB 左右。60 秒、10 records/s 轮共写入 600 个 object，raw 总量为 890,956,800 bytes，约 849.7 MiB。

所有最终纳入对比的结果均通过确定性 raw、SHA-256、PRAGMA integrity_check、外键检查和 object ready 状态检查。P5 早期实现中曾出现 list 字段名和小对象 segments 长度错误，已经修复并重新执行；报告只采用修复后的最终轮次。

## 3. 不同数据量下的表现

### 3.1 常规 profile 的整体吞吐

下面数据为 8 个尺寸、总 raw 93,668,352 bytes 的 hash-on profile 结果。吞吐越高越好；事务 p95 越低越好。

| chunk | SQLite BLOB raw MiB/s | SQLite 事务 p95 | 本地文件 raw MiB/s | 本地文件事务 p95 |
| ---: | ---: | ---: | ---: | ---: |
| 64 KiB | 20.7262 | 1,649.28 ms | 44.9153 | 0.19195 ms |
| 256 KiB | 22.3838 | 1,525.73 ms | 46.3177 | 0.20205 ms |
| 1 MiB | 22.5964 | 1,531.98 ms | 46.5344 | 0.16900 ms |

~~~mermaid
xychart-beta
    title "常规 profile：chunk 对 raw 吞吐的影响"
    x-axis ["64 KiB", "256 KiB", "1 MiB"]
    y-axis "raw MiB/s" 0 --> 50
    line [20.7262, 22.3838, 22.5964]
    line [44.9153, 46.3177, 46.5344]
~~~

图中两条线按表格顺序分别为 SQLite BLOB、本地文件。结论是：

- 256 KiB 到 1 MiB 对吞吐的改善已经很小，256 KiB 是更保守的默认值；
- 本地文件的 metadata 事务 p95 约为 0.17-0.20 ms；
- SQLite BLOB 的事务 p95 约为 1.53-1.65 s，主要成本来自大 BLOB 写入、WAL 和同步提交，而不是 writer mutex 等待。

### 3.2 按单个 raw 对象大小观察延迟

下表是三个 chunk profile 轮次中相同对象大小的样本中位数。表中端到端和事务时间单位为毫秒；它反映“单个对象大小增长”而不是连续吞吐。

| 单对象 raw 大小 | SQLite 端到端中位数 | SQLite 事务中位数 | 本地文件端到端中位数 | 本地文件事务中位数 |
| ---: | ---: | ---: | ---: | ---: |
| 1 KiB | 1.683 ms | 0.266 ms | 3.810 ms | 0.178 ms |
| 16 KiB | 0.975 ms | 0.623 ms | 3.088 ms | 0.143 ms |
| 64 KiB | 2.096 ms | 1.379 ms | 3.988 ms | 0.145 ms |
| 256 KiB | 7.127 ms | 4.351 ms | 8.368 ms | 0.169 ms |
| 1 MiB | 33.829 ms | 21.720 ms | 29.742 ms | 0.160 ms |
| 8 MiB | 361.763 ms | 239.076 ms | 169.064 ms | 0.169 ms |
| 16 MiB | 704.374 ms | 491.422 ms | 345.616 ms | 0.152 ms |
| 64 MiB | 2,875.216 ms | 2,092.273 ms | 1,362.981 ms | 0.157 ms |

~~~mermaid
xychart-beta
    title "大对象：端到端中位数"
    x-axis ["1 MiB", "8 MiB", "16 MiB", "64 MiB"]
    y-axis "milliseconds" 0 --> 3000
    line [33.829, 361.763, 704.374, 2875.216]
    line [29.742, 169.064, 345.616, 1362.981]
~~~

两条线按表格顺序分别为 SQLite BLOB、本地文件。2 MiB 图片请求没有执行专门的 image-only profile，但它位于 1 MiB 和 8 MiB 测试点之间，能够说明趋势：

- SQLite BLOB 的事务时间会随着 raw 大小显著增长；
- 本地文件将大对象写入移出 SQLite 事务，事务时间基本不随 raw 大小增长；
- 本地文件端到端时间仍受 SHA-256、文件写入和 fsync 影响，但在 8-64 MiB 范围明显低于 SQLite BLOB；
- 64 MiB 长对象是当前测试中最明显的长尾来源，不能因为 1 KiB 小对象表现正常就忽略大对象成本。

### 3.3 SHA-256 成本

同一 profile 关闭 hash 后的 raw 吞吐如下：

| backend | hash-on raw MiB/s | hash-off raw MiB/s | hash-on 相对 hash-off |
| --- | ---: | ---: | ---: |
| SQLite BLOB，64/256/1024 KiB | 20.726-22.596 | 25.483-29.845 | 约下降 12%-31% |
| 本地文件，64/256/1024 KiB | 44.915-46.534 | 87.476-90.012 | 约下降 47%-50% |

hash 是业务正确性校验成本，不建议因为关闭 hash 后吞吐更高就从生产路径删除。容量和吞吐评估应按 hash-on 结果进行。

## 4. 持续写入和队列稳定性

### 4.1 60 秒稳定轮

| offered write rate | backend | 实际 wall throughput | raw MiB/s | queue peak | end-to-end p95 | 结果 |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| 10 records/s | SQLite BLOB | 9.9661 records/s | 14.1134 | 4/64 | 241.667 ms | 稳定，PASS |
| 10 records/s | 本地文件 | 9.98557 records/s | 14.1409 | 2/64 | 81.892 ms | 稳定，PASS |
| 50 records/s | SQLite BLOB | 23.4652 records/s | 33.2299 | 64/64 | 3,476.480 ms | 队列饱和 |
| 50 records/s | 本地文件 | 42.9005 records/s | 60.7530 | 64/64 | 1,832.010 ms | 队列饱和 |

~~~mermaid
xychart-beta
    title "混合写入：输入速率与实际吞吐"
    x-axis ["10/s", "50/s"]
    y-axis "actual records/s" 0 --> 50
    line [9.9661, 23.4652]
    line [9.98557, 42.9005]
~~~

两条线按表格顺序分别为 SQLite BLOB、本地文件。短轮还执行过 100/s 和 500/s，两个后端均出现 queue=64/64 饱和，因此不能把 50 records/s 作为当前机器的稳定承诺。

### 4.2 对当前业务的含义

10 records/s 稳定轮使用的是混合对象，平均 raw 大约为 1.414 MiB/record，对应约 14.1 MiB/s。业务中的图片对象约为 2 MiB，因此如果图片占比很高，10 records/s 对应的 raw 写入会接近 20 MiB/s，不能直接认为已经被当前 mixed 轮完整覆盖。

本次结果更适合作为以下保守判断：

- 长时间稳定写入目标先按 10 records/s 以内规划；
- 2 MiB 图片占比较高时，必须把 raw MiB/s 和记录数/s 同时纳入限流；
- 50 records/s 已经在当前 mixed profile 下失稳，不能仅通过增加 queue 容量解决，根因是后端和磁盘处理能力不足以吸收持续输入。

## 5. 读取、导出、清理和故障恢复

### 5.1 并发读取

在独立 writer 持续写入时执行 list、detail、range、segments 读取，各最终轮次均通过正确性检查。

| 查询类型 | SQLite p50 / p95 | 本地文件 p50 / p95 |
| --- | ---: | ---: |
| list | 76 / 154 us | 73 / 138 us |
| detail | 23 / 45 us | 22 / 37 us |
| range | 2,136.5 / 3,337.65 us | 2,270.5 / 3,101.25 us |
| segments | 1,958.5 / 6,793.75 us | 1,618.5 / 2,216.35 us |

两种方案的元数据读取差异不大；真正拉开差距的是大 raw 的写入事务和导出竞争，而不是 list/detail 查询。

### 5.2 导出竞争

60 秒、10 records/s、8 个 seed object、export concurrency=1 的结果如下。无限速和限速 ZIP 都包含 93,668,352 bytes raw，并通过 Python zipfile -t。

| backend | 无导出 p95 | 无限速导出 p95 | 8 MiB/s 限速 p95 | 限速事务 p95 | 无限速 ZIP 耗时 | 限速 ZIP 耗时 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SQLite BLOB | 241.667 ms | 322.522 ms | 339.182 ms | 231.474 ms | 2.108 s | 15.719 s |
| 本地文件 | 81.892 ms | 178.969 ms | 177.559 ms | 0.4182 ms | 1.496 s | 15.719 s |

~~~mermaid
xychart-beta
    title "导出期间写入端到端 p95"
    x-axis ["无导出", "无限速", "8 MiB/s"]
    y-axis "milliseconds" 0 --> 400
    line [241.667, 322.522, 339.182]
    line [81.892, 178.969, 177.559]
~~~

两条线按表格顺序分别为 SQLite BLOB、本地文件。结论：

- 限速将导出时间从约 1.5-2.1 秒增加到约 15.7 秒；
- 限速没有改善两个后端的写入 p95；
- 本地文件的写入事务仍为微秒级，SQLite BLOB 的事务 p95 约为 231 ms；
- 生产实现应限制导出并发、让导出进入后台队列，并避免导出高峰和大量图片写入高峰重叠；不能只设置一个 8 MiB/s 的读取限速。

### 5.3 清理和 WAL

| 项目 | SQLite BLOB | 本地文件 |
| --- | ---: | ---: |
| 清理前 WAL | 135,173,112 bytes | 589,192 bytes |
| wal_checkpoint(TRUNCATE) 后 WAL | 0 | 0 |
| 清理前 raw/数据库主要占用 | 数据库 254,738,432 bytes | raw 187,336,704 bytes |
| 清理后 raw/数据库主要占用 | 主数据库仍 254,738,432 bytes；freelist 增加 | raw 93,323,264 bytes；过期 raw 已删除 |

SQLite 清理可以回收 WAL，但不会自动缩小主数据库文件；后续若要回收主数据库空间，需要独立维护窗口评估 VACUUM。本地文件可以直接删除过期 raw，但必须保证 metadata 删除、raw 删除和导出保护之间的顺序正确。

### 5.4 异常退出和恢复

共执行 8 个故障轮：两种 backend 分别覆盖 raw 写入中断、提交前中断、清理提交前中断和导出中断。

- 8/8 正确性检查通过；
- SQLite 没有产生 ready 半成品；
- 本地文件 raw 写入/提交中断后清理了 2 个 orphan raw；
- 导出中断没有发布半成品 ZIP；
- 两种方案均通过数据库完整性、外键、ready 状态、locator 和 raw 校验。

这组结果说明本地文件方案可行，但 orphan sweep、临时文件清理和一致性备份必须作为正式功能，而不能只依赖异常概率较低的假设。

## 6. 结合当前 200 人内网场景的容量评估

### 6.1 规模换算

| 项目 | 低位 | 中位估算 | 高位 |
| --- | ---: | ---: | ---: |
| 用户数 | 200 | 200 | 200 |
| 每用户 project | 2 | 2.5 | 3 |
| project 数 | 400 | 500 | 600 |
| 每 project protocol | 5 | 5.5 | 6 |
| project-protocol 关系 | 2,000 | 2,750 | 3,600 |
| 每 project 至少图片 protocol | 1 | 1 | 1 |

project-protocol 关系是配置和查询规模，不等于每秒写入记录数。实际写入速率取决于 protocol 被执行的频率。

### 6.2 图片 raw 容量

以下按“每个 project 在一个业务周期内产生 1 个 2 MiB 图片请求”估算，不包括其他 protocol 的 raw、metadata、SQLite WAL、ZIP、备份副本和文件系统预留空间。

| 图片请求周期 | 400 projects | 500 projects | 600 projects |
| --- | ---: | ---: | ---: |
| 一个周期 raw | 800 MiB / 0.781 GiB | 1,000 MiB / 0.977 GiB | 1,200 MiB / 1.172 GiB |
| 每天一个周期，30 天 | 23.4 GiB | 29.3 GiB | 35.2 GiB |
| 每小时一个周期，30 天 | 562.5 GiB | 703.1 GiB | 843.8 GiB |

月度 raw 容量可使用以下公式估算：

~~~text
monthly_raw_GiB
  = project_count
  * image_requests_per_project_per_day
  * 2 MiB
  * retention_days
  / 1024
~~~

实际容量规划至少还应加上：

- 其他 4-5 个 protocol 的 request/response raw；
- metadata、索引和 SQLite WAL；
- ZIP 导出临时空间；
- 备份副本；
- 文件系统和 orphan 清理失败的应急空间。

建议先按 raw 估算值的 1.3-1.5x 预留在线存储空间，再根据真实 payload 分布和 retention 策略调整。这个系数是容量规划保守余量，不是本次 benchmark 测得的固定开销。

### 6.3 请求频率和写入压力

#### 只看图片 protocol

| 每个 project 的图片请求周期 | 图片 records/s：400 / 500 / 600 projects | 图片 raw MiB/s：400 / 500 / 600 projects |
| --- | ---: | ---: |
| 每小时 1 次 | 0.111 / 0.139 / 0.167 | 0.222 / 0.278 / 0.333 |
| 每分钟 1 次 | 6.667 / 8.333 / 10.000 | 13.333 / 16.667 / 20.000 |
| 每 10 秒 1 次 | 40.000 / 50.000 / 60.000 | 80.000 / 100.000 / 120.000 |

中心估算下，如果每个 project 每分钟都生成一次 2 MiB 图片，图片 alone 就是 8.333 records/s、16.667 MiB/s，已经接近本次 10 records/s 保守稳定线；若其他 protocol 同时产生历史记录，总记录速率会超过 10/s。

#### 每个 project 执行完整 5.5 个 protocol

以下按中心估算的 500 个 project、每次执行 5.5 个 protocol 计算。图片 raw 只按每轮 1 个 2 MiB 图片计算，其他 protocol raw 未计入。

| 完整 protocol 扫描周期 | 总 records/s | 其中图片 records/s | 仅图片 raw MiB/s | 与测试结果的关系 |
| --- | ---: | ---: | ---: | --- |
| 每小时 1 次 | 0.764 | 0.139 | 0.278 | 明显低于 10/s 保守线 |
| 每 5 分钟 1 次 | 9.167 | 1.667 | 3.333 | 接近 10/s，需控制并发 |
| 每分钟 1 次 | 45.833 | 8.333 | 16.667 | 接近 50/s，当前 mixed 轮存在失稳风险 |
| 每 10 秒 1 次 | 275.000 | 50.000 | 100.000 | 明显超过本次单机测试能力 |

因此，当前场景是否能稳定运行不能仅由“200 用户”判断：

- 用户数和 project 数决定待管理对象总量及一次全量扫描的大小；
- protocol 执行周期决定实时写入压力；
- 图片 protocol 决定 raw MiB/s 和容量增长速度；
- 导出和清理是否与扫描高峰重叠决定 p95 是否进一步恶化。

### 6.4 对当前业务的容量和性能判断

基于当前测试，建议将以下指标作为第一版运行边界：

| 指标 | 建议边界 | 说明 |
| --- | --- | --- |
| 长时间历史写入 | 目标不超过 10 records/s | 本次 10/s 60 秒两后端稳定；50/s 两后端均饱和 |
| 图片写入带宽 | 先按实际 raw MiB/s 限制 | 2 MiB 图片不能只按 records/s 估算 |
| 单次自动全量扫描 | 建议错峰并限并发 | 中心估算每分钟扫描约 45.8 records/s，已接近失稳区间 |
| 导出并发 | 初始限制为 1 | 长时导出已证明会抬高写入 p95 |
| chunk | 256 KiB | 目前与 1 MiB 吞吐接近，内存和调度更保守 |
| SQLite | WAL + synchronous=NORMAL | 现有测试配置；FULL 尚未完成对照 |
| retention | 必须先明确天数和上限 | 图片 raw 会成为主要容量消耗 |

## 7. 方案对照与最终建议

### 7.1 性能、正确性和运维对照

| 维度 | SQLite BLOB | SQLite + 本地文件 |
| --- | --- | --- |
| 常规 profile 吞吐 | 20.7-22.6 MiB/s | 44.9-46.5 MiB/s |
| 2 MiB 以上对象事务 | 随对象大小明显增长 | metadata 事务约 0.15-0.20 ms |
| 10/s 长时写入 | 稳定，p95 241.7 ms | 稳定，p95 81.9 ms |
| 50/s 长时写入 | 23.465 records/s，queue 饱和 | 42.901 records/s，queue 饱和 |
| 列表/详情读取 | 微秒级 | 微秒级 |
| 局部 payload 读取 | 正确，受 BLOB 读取成本影响 | 正确，range/segments 更适合文件读取 |
| 导出 | ZIP 正确，事务成本高，WAL 大 | ZIP 正确，导出更快，I/O 仍会影响写入 |
| 清理 | WAL 可清空，主库不会自动缩小 | raw 可直接删除，但要保护和扫孤儿 |
| 异常恢复 | 事务边界自然 | 需要 temp/rename/orphan sweep |
| 备份 | 单文件路径简单 | 必须保证数据库与 raw 目录一致 |
| 当前建议 | 部署极简或低写入量备选 | 当前性能优先推荐 |

### 7.2 推荐落地架构

~~~text
协议交互完成
  -> 事务外计算 SHA-256
  -> raw 写入临时文件
  -> fsync 临时文件
  -> 原子 rename 为 object_key
  -> SQLite 短事务写 record/object/payload locator
  -> 提交后 object state = ready

后台任务
  -> orphan raw sweep
  -> retention 清理
  -> 导出保护检查
  -> SQLite checkpoint
  -> 容量、WAL、raw 和导出任务监控
~~~

推荐使用稳定且不暴露绝对路径的 object key，例如按日期、project、record 和随机后缀分层；payload locator 继续只保存 raw byte offset/length，不保存机器绝对路径。

### 7.3 必须实现的可靠性边界

在正式接入业务前，本地文件方案至少需要覆盖以下状态：

1. raw 临时文件已创建但 SQLite 事务尚未提交；
2. SQLite 已提交但进程在状态更新前退出；
3. 删除 metadata 成功但 raw 删除失败；
4. 导出进行中时 retention 任务尝试删除 raw；
5. 数据库备份完成但 raw 目录仍在写入；
6. 启动时发现 orphan、缺失 raw 或 checksum 不匹配。

任何缺失 raw、locator 越界、ready object hash 错误或导出缺字节都应进入错误状态和告警，不应静默返回空 payload。

## 8. 后续验证和上线前门槛

本轮历史结果已经足够进行候选方案排序，但还不应直接宣称完成生产容量验收。benchmark 代码已切换到最终 schema，下一轮应先按最终 schema 重跑核心 P0-P9，再补充剩余项目：

| 项目 | 原因 | 建议触发条件 |
| --- | --- | --- |
| 2 MiB image-only profile | 当前 profile 没有单独覆盖业务主导对象 | 实现图片 protocol 后执行 |
| 真实设备 payload 分布 | synthetic 数据不能代表实际压缩率、响应大小和失败比例 | 接入一批典型设备后执行 |
| 10 分钟稳定性轮 | 当前 50/s 已在 60 秒失稳，因此没有强行执行更高压力线 | 明确生产最低写入线后执行 |
| synchronous=FULL | 当前只有 NORMAL 结果 | 需要更强断电耐久性时执行 |
| RSS/CPU 外部采样 | 当前 summary 未记录进程级资源曲线 | 评估并发导出和大图片峰值时执行 |
| 周期性 WAL/checkpoint 曲线 | 当前记录的是最终 WAL 大小 | 评估长时间导出和 retention 时执行 |
| VACUUM 维护窗口 | SQLite 清理后主库文件不缩小 | retention 周期和磁盘预算确定后执行 |
| 备份恢复演练 | benchmark 只验证进程中断，不等于备份一致性 | 方案确定后执行数据库/raw 联合恢复 |
| 最终 schema 重跑 | 当前报告数据来自旧 schema，代码已改为最终 interaction_payload_refs.ranges_json 契约 | 本次编译通过后执行新一轮 P0-P9 |

## 9. 可复现结果索引

主要历史结果目录如下，所有目录均位于 tests/benchmarks/results/，并由 .gitignore 忽略。注意：这些结果用于趋势对照，尚未按最终 schema 重跑。

| 场景 | SQLite BLOB | 本地文件 |
| --- | --- | --- |
| profile，hash-on，64/256/1024 KiB | 20260723-193617-*、193656-*、193749-* | 20260723-193832-*、193836-*、193839-* |
| mixed，10/s，60 秒 | 20260723-201403-sqlite_blob-mixed/ | 20260723-201801-local_file-mixed/ |
| mixed，50/s，60 秒 | 20260723-212030-sqlite_blob-mixed/ | 20260723-212424-local_file-mixed/ |
| 并发读取 | 20260723-211503-sqlite_blob-read/ | 20260723-211512-local_file-read/ |
| 长时无限速导出 | 20260723-220243-sqlite_blob-export/ | 20260723-220418-local_file-export/ |
| 长时 8 MiB/s 限速导出 | 20260723-221146-sqlite_blob-export/ | 20260724-105124-local_file-export/ |
| 清理和 WAL | 20260723-210256-sqlite_blob-cleanup/ | 20260723-210308-local_file-cleanup/ |
| 异常退出恢复 | 20260723-211043-*、211044-* | 20260723-211043-*、211044-* |

详细执行命令、每个 P0-P9 子测试的状态和剩余风险见：

- SQLite 历史存储基准测试计划.md
- SQLite 历史存储基准测试执行进度.md

## 10. 最终结论

针对当前约 200 人内网、400-600 个 project、每个 project 5-6 个 protocol 且至少一个约 2 MiB 图片请求的场景：

1. 推荐采用 SQLite 元数据 + 本地文件 raw，而不是把 2 MiB 及更大的 raw 放入 SQLite BLOB 事务；
2. 第一版将长期历史写入目标控制在 10 records/s 以内，并同时设置 raw MiB/s 限制；
3. 自动 protocol 扫描必须错峰、限并发并设置队列背压，尤其要避免中心估算下每分钟全量扫描造成约 45.8 records/s 的峰值；
4. 图片历史容量应按项目数量、图片请求频率和 retention 天数单独计算，不能只按用户数量估算；
5. 导出必须后台化，初始 concurrency 建议为 1，并与 retention 清理、raw 删除和备份流程建立保护关系；
6. 本地文件方案的原子落盘、孤儿清理、联合备份和恢复演练完成前，不能把本次性能优势直接视为生产可靠性已经满足。
