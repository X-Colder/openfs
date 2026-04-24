# OpenFS 开发方案

## 一、开发阶段规划

整体采用**增量迭代**策略，分 6 个阶段，每个阶段产出可运行、可验证的最小系统，逐步叠加能力。

```
Phase 1: 单机原型 (4 周)
    → 单 MetaNode + 单 DataNode，跑通基本读写
Phase 2: Segment 存储引擎 (3 周)
    → Segment 日志式存储 + 多级 Block + PackedBlock
Phase 3: 多级缓存 (3 周)
    → L1/L2/L3 三级缓存 + 硬件感知 + 缓存一致性
Phase 4: 集群化 (5 周)
    → Raft 元数据 + 多 DataNode + 心跳 + Block 分配
Phase 5: 高级特性 (4 周)
    → 故障修复 + Rebalance + 热度感知 + 自适应保护策略
Phase 6: 接入层 (4 周)
    → FUSE + S3 Gateway + CLI + 性能调优
```
Phase 6: 接入层 (4 周)
    → FUSE + S3 Gateway + CLI + 性能调优
```

---

## 二、Phase 1：单机原型（4 周）

**目标**：单进程 MetaNode + 单进程 DataNode，Client SDK 跑通文件创建、写入、读取。

### 第 1 周：项目脚手架 + 公共基础

| 任务 | 产出 |
|---|---|
| CMake 项目搭建 | CMakeLists.txt, 第三方依赖集成 |
| Protobuf 定义 | common.proto, meta_service.proto, data_service.proto |
| 公共类型定义 | src/common/types.h (Inode, BlockMeta, BlockLevel 等) |
| CRC32 工具 | src/common/crc32.h/cpp |
| ID 生成器 | src/common/id_generator.h/cpp |
| 配置管理 | src/common/config.h/cpp |
| 日志封装 | src/common/logging.h (基于 spdlog) |
| 单元测试框架 | tests/ 目录结构, GTest 集成 |

**验证标准**：项目可编译，CRC32 和 ID 生成器单测通过。

### 第 2 周：MetaNode 单机版

| 任务 | 产出 |
|---|---|
| RocksDB 存储层 | MetadataEngine 基础 CRUD |
| Inode 管理 | InodeTable: create / get / delete / update |
| 目录树管理 | NamespaceManager: mkdir / readdir / lookup / rename |
| Block 映射表 | BlockMapTable: 记录 FileID → BlockID 列表 |
| RPC 服务 | MetaService: CreateFile, GetFileInfo, MkDir, ReadDir |
| 单机 Block 分配 | BlockAllocator: 简单版（单节点分配） |

**验证标准**：通过 RPC 创建目录、创建文件、查询文件信息。

### 第 3 周：DataNode 单机版

| 任务 | 产出 |
|---|---|
| Segment 基础写入 | SegmentWriter: 追加写入 Block 到 Segment 文件 |
| Segment 基础读取 | SegmentReader: 按 offset 读取 Block |
| 本地索引 | SegmentIndex: BlockID → Segment + Offset 映射 |
| Segment 封存 | SegmentSealer: 写满 256MB 后 seal |
| RPC 服务 | DataService: WriteBlock, ReadBlock |
| CRC 校验 | 写入时校验，读取时验证 |

**验证标准**：直接调用 DataNode RPC 写入/读取 Block，CRC 校验通过。

### 第 4 周：Client SDK + 端到端联调

| 任务 | 产出 |
|---|---|
| Client 基础框架 | OpenFSClient: 连接管理, 基本接口 |
| 写入流程 | CreateFile → AllocateBlocks → WriteBlock → CommitBlocks |
| 读取流程 | Lookup → GetBlockLocations → ReadBlock |
| Block 切分 | BlockSplitter: 根据文件大小自动选择 Block Level |
| 端到端测试 | 写入文件 → 读取文件 → 校验数据一致 |

**验证标准**：Client 写入 1MB/50MB/500MB 文件，读取回来数据完全一致。

---

## 三、Phase 2：Segment 存储引擎（3 周）

**目标**：完善 Segment 日志式存储，实现多级 Block 和 PackedBlock。

### 第 5 周：Segment 引擎完善

| 任务 | 产出 |
|---|---|
| Segment Header/Footer | 完整的二进制格式实现 |
| 多 Segment 管理 | SegmentEngine: 管理多个 Segment 文件的生命周期 |
| 4KB 对齐 | Block 数据写入对齐到 4KB 边界 |
| Segment 完整性 | seal 时写入 Footer 含全量索引 + 总 CRC |
| 启动恢复 | DataNode 重启时从 Segment 文件重建本地索引 |

**验证标准**：DataNode 重启后数据可正常读取，Segment 格式校验通过。

### 第 6 周：多级 Block + PackedBlock

| 任务 | 产出 |
|---|---|
| 多级 Block 选择 | BlockSplitter 完整实现 L0~L4 自动选择逻辑 |
| PackedBlock 写入 | 小文件合并打包写入 PackedBlock |
| PackedBlock 读取 | 按 intra_offset 精确读取 PackedBlock 内的单个文件 |
| 元数据适配 | PackedBlockEntry 在 MetaNode 的存储与查询 |
| 流式写入支持 | 大小未知的流式写入，先用 L2 切分 |

**验证标准**：
- 1000 个 10KB 小文件打包后空间占用 < 12MB（接近 10MB 理论值）
- 100GB 大文件用 L4 切分，元数据条目数 < 400
- 各级别 Block 混合写入和读取正确

### 第 7 周：追加批次分条（Append-Batched-Striping）

| 任务 | 产出 |
|---|---|
| BatchBuffer | Client 侧 Block 批次缓冲（16 个 Block 或 64MB） |
| 批次写入 | 一批 Block 顺序发送到同一 DataNode |
| MetaNode 分配优化 | AllocateBlocks 返回同一节点的连续分配 |
| 写入管线 | WritePipeline: 异步批次发送 + 回调确认 |

**验证标准**：写入吞吐提升显著（对比逐 Block 随机分配），fio 测试顺序写带宽接近磁盘上限。

---

## 四、Phase 3：多级缓存（3 周）

**目标**：实现 L1/L2/L3 三级缓存体系，硬件感知自适应，保证缓存一致性。

### 第 8 周：L1 Client 缓存 + L2 DataNode DRAM 缓存

| 任务 | 产出 |
|---|---|
| L1 MetadataCache | Client 侧 inode/dentry/block_location 缓存，LRU 淘汰 |
| L1 DataCache | Client 侧读缓存，最近读取的 Block 数据，可配置 256MB-4GB |
| 缓存版本号机制 | 每个缓存条目携带 version + expire_time |
| Version + Lease 校验 | CheckVersion RPC，过期条目向 MetaNode 校验有效性 |
| L2 HotBlockCache | DataNode DRAM 缓存，LRU-2 淘汰，防扫描污染 |
| L2 BloomFilter | Block 存在性快速判定，避免无效磁盘查找 |

**验证标准**：
- 热 Block 重复读取命中 L1/L2 缓存，零磁盘 IO
- 文件修改后其他 Client 缓存自动失效
- Lease 过期后 Client 正确校验版本

### 第 9 周：L3 NVMe 缓存层

| 任务 | 产出 |
|---|---|
| NVMeCacheEngine | NVMe 上的缓存 Segment 文件 + RocksDB 索引 |
| 准入策略 | 访问 ≥ 2 次才缓存、小文件优先、顺序关联准入 |
| 淘汰策略 | 已删除优先淘汰、大冷 Block 优先淘汰、有副本的优先淘汰 |
| 读取路径集成 | L2 miss → L3 NVMe → HDD，命中后回填上级缓存 |
| Write-Back 模式 | 可选的 NVMe 写缓存 + WAL 保护 + 定期 flush |
| Write-Through/Back 切换 | 元数据根据路径/标签自动选择写入模式 |

**验证标准**：
- NVMe 缓存命中时读延迟 < 200μs，HDD 读延迟 > 3ms，差异明显
- Write-Back 模式下写入吞吐提升 3x+（vs HDD 直写）
- 断电恢复后 WAL 中的数据不丢失

### 第 10 周：硬件感知 + 缓存预热

| 任务 | 产出 |
|---|---|
| 硬件探测 | DataNode 启动时自动探测存储设备类型和性能基线 |
| 自动角色分配 | NVMe 自动识别为缓存层或存储层（视有无 HDD） |
| 心跳扩展 | DataNode 上报缓存容量、使用率、命中率、设备信息 |
| 主动预热 | openfs-cli cache warmup 命令，指定目录数据加载到 NVMe |
| 智能预热 | 检测顺序访问模式时自动预读后续 Block 到 NVMe |
| 缓存统计 | 各级缓存命中率、容量使用率的监控指标 |

**验证标准**：
- NVMe + HDD 混合节点正确识别设备角色
- cache warmup 后训练数据读取全部命中 NVMe，无 HDD IO
- 缓存命中率统计数据准确

---

## 五、Phase 4：集群化（5 周）

**目标**：元数据 Raft 高可用，多 DataNode 集群调度。

### 第 11 周：Raft 集成

| 任务 | 产出 |
|---|---|
| braft 集成 | CMake 引入 braft 依赖 |
| RaftStateMachine | 元数据操作封装为 Raft 日志 |
| 快照管理 | SnapshotManager: RocksDB checkpoint 作为 Raft 快照 |
| Leader 选举 | 3 节点 MetaCluster 自动选主 |
| 读写路由 | Client 自动发现 Leader，写请求路由到 Leader |

**验证标准**：3 MetaNode 集群，kill Leader 后自动切换，Client 写入不中断。

### 第 12 周：多 DataNode 调度

| 任务 | 产出 |
|---|---|
| NodeRegistry | DataNode 注册到 MetaCluster |
| HeartbeatMonitor | 心跳接收 + 节点状态管理（ONLINE/SUSPECT/OFFLINE） |
| BlockAllocator 增强 | 基于节点负载的 Block 分配策略 |
| 批次轮转 | 不同批次分配到不同 DataNode，保证均衡 |
| 多节点并行读 | Client ReadPipeline 从多节点并行拉取 |

**验证标准**：3+ DataNode 集群，写入数据均匀分布，读取时多节点并行。

### 第 13 周：节点管理与故障检测

| 任务 | 产出 |
|---|---|
| 故障判定 | 心跳超时 → SUSPECT → OFFLINE 状态机 |
| Block 受影响分析 | OFFLINE 节点上的 Block 列表快速查询 |
| 节点上下线通知 | 事件驱动架构，通知各模块节点状态变化 |
| DiskMonitor | DataNode 磁盘健康监控（使用率、IO 延迟、SMART） |
| 管理 API | AdminService: 查询集群状态、节点列表 |

**验证标准**：手动 kill DataNode，MetaNode 30s 内标记 OFFLINE，管理 API 可查询。

### 第 14-15 周：Subtree Partitioning

| 任务 | 产出 |
|---|---|
| 分区路由表 | 路径前缀 → Raft Group 映射 |
| Multi-Raft | 同一 MetaNode 进程承载多个 Raft Group |
| Client 路由 | Client 根据路径前缀选择正确的 Raft Group |
| 路由缓存 | Client 缓存路由表，Leader 变更时自动刷新 |
| 分区分裂 | 子树过大时自动拆分为新的 Raft Group |

**验证标准**：5 MetaNode 承载 4+ Raft Group，不同路径的操作路由到正确的 Group。

---

## 六、Phase 5：高级特性（4 周）

**目标**：故障自愈、数据均衡、热度感知、自适应保护。

### 第 16 周：Block 修复

| 任务 | 产出 |
|---|---|
| RepairManager | 故障节点 Block 修复调度 |
| 并行修复 | 修复任务分散到多个 DataNode 并行执行 |
| 优先级修复 | 热 Block 优先修复 |
| 修复限流 | 不超过节点带宽的 30% |
| DataNode 间传输 | TransferService: DataNode 直接传输 Block |

**验证标准**：kill 1 个 DataNode（有副本的 Block），Block 自动修复到其他节点。

### 第 17 周：Rebalance

| 任务 | 产出 |
|---|---|
| RebalanceManager | 全局 Block 分布计算 + 迁移计划生成 |
| 增量迁移 | 后台逐步迁移 Block，不影响在线业务 |
| 迁移限流 | 不超过节点带宽的 20% |
| 新节点加入触发 | 新 DataNode 注册后自动启动 Rebalance |

**验证标准**：3 节点写满数据后加入第 4 节点，Block 自动均衡，使用率偏差 < 5%。

### 第 18 周：热度感知 + 自适应保护

| 任务 | 产出 |
|---|---|
| HeatTracker | Block 访问频次统计 + 热度衰减 |
| 热 Block 副本升级 | 高频访问 Block 自动增加副本 |
| 冷 Block 副本降级 | 低频访问 Block 降为单副本回收空间 |
| 自适应保护策略 | 根据集群节点数自动调整默认副本数 |
| 后台巡检 | BackgroundScrubber: 定期 CRC 全量校验 |

**验证标准**：
- 高频读取某文件，该文件 Block 副本数自动升至 2
- 停止读取后副本数自动降回 1
- 后台巡检发现篡改数据能触发修复

### 第 19 周：数据删除与空间回收

| 任务 | 产出 |
|---|---|
| 文件删除流程 | 删除文件 → 标记 Block 待回收 → 延迟删除 |
| Segment GC | 封存 Segment 中有效 Block 率过低时，Compact 到新 Segment |
| PackedBlock GC | PackedBlock 中删除文件后的空间回收 |
| 缓存联动清理 | 删除文件时同步清除 L2/L3 缓存中的对应 Block |
| 空间统计 | 各节点实际/逻辑使用量统计 |

**验证标准**：删除大量文件后，GC 回收空间，磁盘使用率下降。

---

## 七、Phase 6：接入层（4 周）

**目标**：FUSE 文件系统、S3 Gateway、CLI 工具、性能调优。

### 第 20 周：FUSE 文件系统

| 任务 | 产出 |
|---|---|
| FUSE 适配 | openfs_fuse: 实现 libfuse3 的回调接口 |
| 基本操作 | open/read/write/close/stat/mkdir/readdir/rename/unlink |
| 多线程 | FUSE 多线程模式 |
| 读写缓存 | 写缓存 + 读预取 |

**验证标准**：挂载为本地目录，cp/ls/cat/dd 等命令正常工作。

### 第 21 周：S3 Gateway

| 任务 | 产出 |
|---|---|
| HTTP Server | 基于 brpc HTTP 模块 |
| S3 协议适配 | PutObject / GetObject / DeleteObject / ListObjects |
| Bucket 管理 | CreateBucket / DeleteBucket / ListBuckets |
| Multipart Upload | 大对象分段上传 |
| Range Read | 范围读取 |

**验证标准**：使用 aws-cli 或 s3cmd 进行基本的对象操作。

### 第 22 周：CLI 管理工具

| 任务 | 产出 |
|---|---|
| openfs-cli 框架 | 命令行解析 + 子命令注册 |
| 集群管理命令 | cluster status / node list / node add |
| 文件操作命令 | ls / stat / cp / rm / mkdir |
| 缓存管理命令 | cache warmup / cache stat / cache evict |
| 诊断命令 | check block / check segment / fsck |
| 运维脚本 | start/stop/deploy 脚本 |

**验证标准**：openfs-cli 可以管理集群、操作文件、管理缓存。

### 第 23 周：性能调优 + 集成测试

| 任务 | 产出 |
|---|---|
| io_uring 优化 | DataNode 全面使用 io_uring 异步 IO |
| 零拷贝 | 网络发送/接收零拷贝 |
| 连接池优化 | Client 连接池参数调优 |
| 内存池 | 频繁分配的 Block 缓冲区使用内存池 |
| 缓存端到端调优 | L1/L2/L3 容量配比、淘汰参数调优 |
| 基准测试 | fio + 自定义 benchmark 工具 |
| 压力测试 | 多 Client 并发读写 + 故障注入 |
| 全量集成测试 | 端到端场景覆盖 |

**验证标准**：
- 单节点顺序写 ≥ 1GB/s
- 单节点顺序读 ≥ 2GB/s
- NVMe 缓存命中时读延迟 < 200μs
- 3 节点集群聚合带宽接近线性增长
- 故障注入下数据不丢失

---

## 八、关键技术风险与应对

| 风险 | 影响 | 应对策略 |
|---|---|---|
| braft 编译/兼容性问题 | 阻塞 Phase 4 | Phase 1 即引入依赖，提前验证编译 |
| io_uring 内核版本依赖 | 部分系统不支持 | 提供 fallback 到 libaio 的适配层 |
| 单副本数据丢失风险 | 节点永久故障时数据不可恢复 | 关键数据自动双副本；用户可配置保护级别 |
| Subtree Partitioning 复杂度 | 跨分区 rename 等操作困难 | 先实现单 Raft Group，分区功能逐步迭代 |
| 小文件 PackedBlock GC | 删除操作后空间碎片 | Compact 策略 + 有效率阈值触发重写 |
| NVMe Write-Back 断电丢数据 | 未 flush 数据丢失 | WAL 保护 + 定期 flush + 关键数据强制 Write-Through |
| 缓存击穿/雪崩 | 大量请求穿透到 HDD | BloomFilter + 热点数据 Pin + 限流保护 |

---

## 九、第三方依赖清单

| 依赖 | 版本 | 用途 | 许可证 |
|---|---|---|---|
| brpc | 1.x | RPC 框架 | Apache 2.0 |
| braft | 1.x | Raft 共识 | Apache 2.0 |
| RocksDB | 8.x | KV 存储引擎 | Apache 2.0 / GPL 2.0 |
| protobuf | 3.x | 序列化 | BSD 3-Clause |
| liburing | 2.x | io_uring 封装 | LGPL / MIT |
| libfuse | 3.x | FUSE 接口 | LGPL 2.1 |
| spdlog | 1.x | 日志库 | MIT |
| gtest | 1.x | 测试框架 | BSD 3-Clause |
| gflags | 2.x | 命令行参数 | BSD 3-Clause |

---

## 十、测试策略

### 9.1 单元测试

- 覆盖所有核心模块（CRC32、Segment 引擎、Block 分配器、元数据操作）
- 每个 Phase 结束前单测覆盖率 ≥ 80%
- 使用 Google Test 框架

### 9.2 集成测试

- 单节点端到端读写
- 多节点集群读写
- 故障注入（kill 节点、磁盘错误模拟）
- 并发压力测试

### 9.3 性能测试

- 使用 fio 测试裸 IO 性能基线
- 自定义 benchmark 测试各场景：
  - 大文件顺序读写
  - 小文件随机读写
  - 混合负载
  - AI 训练数据加载模拟

---

## 十一、里程碑与交付物

| 里程碑 | 时间 | 交付物 |
|---|---|---|
| M1: 单机原型 | 第 4 周 | 单机读写可用，Client SDK 基础版 |
| M2: 存储引擎 | 第 7 周 | Segment 日志存储 + 多级 Block + PackedBlock |
| M3: 多级缓存 | 第 10 周 | L1/L2/L3 三级缓存 + 硬件感知 + 缓存预热 |
| M4: 集群版 | 第 15 周 | Raft 高可用 + 多 DataNode + 自动调度 |
| M5: 高级特性 | 第 19 周 | 故障自愈 + Rebalance + 热度感知 |
| M6: 完整系统 | 第 23 周 | FUSE + S3 + CLI + 性能达标 |