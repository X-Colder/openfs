# OpenFS 架构设计文档

## 一、系统总体架构

```
┌─────────────────────────────────────────────────────────┐
│                     Client Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │ libclient│  │   FUSE   │  │S3 Gateway│  │openfs-cli│ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │
└───────┼──────────────┼──────────────┼─────────────┼──────┘
        │              │              │             │
        ▼              ▼              ▼             ▼
┌──────────────────────────────────────────────────────────┐
│                   MetaCluster (元数据集群)                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│  │MetaNode 1│◄─►MetaNode 2│◄─►MetaNode 3│  ... (Raft)  │
│  └──────────┘  └──────────┘  └──────────┘               │
│  [Namespace] [DirTree] [BlockMap] [NodeTopo] [Schedule]  │
└───────────────────────┬──────────────────────────────────┘
                        │ Block 位置查询 / 写入分配
                        ▼
┌──────────────────────────────────────────────────────────┐
│                   DataCluster (数据集群)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│  │DataNode 1│  │DataNode 2│  │DataNode 3│  ... (无状态)  │
│  │[Segments]│  │[Segments]│  │[Segments]│               │
│  └──────────┘  └──────────┘  └──────────┘               │
└──────────────────────────────────────────────────────────┘
```

### 数据流概览

- **写入路径**：Client → MetaCluster(分配 Block 位置) → Client 直连 DataNode 写入 → 回调 MetaCluster 确认
- **读取路径**：Client → MetaCluster(查询 Block 位置) → Client 直连 DataNode 读取
- **管理路径**：CLI/API → MetaCluster → DataCluster

---

## 二、MetaNode 详细设计

### 2.1 进程架构

```
MetaNode 进程
├── RPC Server (gRPC/brpc)
│   ├── ClientService      ← 处理 Client 的元数据请求
│   ├── DataNodeService    ← 处理 DataNode 心跳/汇报
│   └── AdminService       ← 处理管理命令
├── RaftEngine
│   ├── RaftStateMachine   ← 元数据状态机
│   ├── RaftLog            ← Raft 日志持久化
│   └── SnapshotManager    ← 快照管理
├── MetadataEngine
│   ├── NamespaceManager   ← 命名空间 & 目录树
│   ├── InodeTable         ← inode 表
│   ├── BlockMapTable      ← Block 映射表
│   └── RocksDBStore       ← 底层 KV 持久化
├── ScheduleEngine
│   ├── BlockAllocator     ← Block 写入分配（选节点、选 Segment）
│   ├── RebalanceManager   ← 后台 Block 均衡迁移
│   ├── RepairManager      ← 故障 Block 修复调度
│   └── HeatTracker        ← 热度跟踪 & 副本升降
└── ClusterManager
    ├── NodeRegistry       ← DataNode 注册管理
    ├── HeartbeatMonitor   ← 心跳检测 & 故障判定
    └── TopoManager        ← 集群拓扑（机架感知）
```

### 2.2 核心数据结构

#### Inode

```cpp
struct Inode {
    uint64_t    inode_id;       // 全局唯一 inode ID
    uint32_t    mode;           // 文件类型 + 权限
    uint32_t    uid;            // 所属用户
    uint32_t    gid;            // 所属用户组
    uint64_t    size;           // 文件大小
    uint64_t    nlink;          // 硬链接数
    Timestamp   atime;          // 访问时间
    Timestamp   mtime;          // 修改时间
    Timestamp   ctime;          // 变更时间
    BlockLevel  block_level;    // Block 等级 (L0-L4)
    bool        is_packed;      // 是否在 PackedBlock 中
};
```

#### BlockMeta

```cpp
struct BlockMeta {
    uint64_t    block_id;       // 全局唯一 Block ID
    BlockLevel  level;          // Block 等级
    uint32_t    size;           // 实际数据大小
    uint32_t    crc32;          // 数据校验和
    uint64_t    node_id;        // 所在 DataNode
    uint64_t    segment_id;     // 所在 Segment
    uint64_t    offset;         // Segment 内偏移
    Timestamp   create_time;    // 写入时间
    uint16_t    replica_count;  // 当前副本数
    uint32_t    access_count;   // 访问计数（热度）
};
```

#### PackedBlockEntry

```cpp
struct PackedBlockEntry {
    uint64_t    file_inode_id;  // 文件 inode
    uint64_t    pack_block_id;  // 所属 PackedBlock 的 block_id
    uint32_t    intra_offset;   // PackedBlock 内偏移
    uint32_t    intra_size;     // 实际数据大小
};
```

### 2.3 RocksDB 存储布局

```
ColumnFamily: "inode"
  Key:   inode_id (uint64)
  Value: Inode (序列化)

ColumnFamily: "dentry"
  Key:   parent_inode_id + "/" + filename
  Value: child_inode_id + file_type

ColumnFamily: "block_map"
  Key:   inode_id + block_index (文件内第几个 Block)
  Value: block_id

ColumnFamily: "block_meta"
  Key:   block_id
  Value: BlockMeta (序列化)

ColumnFamily: "packed_entry"
  Key:   file_inode_id
  Value: PackedBlockEntry (序列化)

ColumnFamily: "node_info"
  Key:   node_id
  Value: NodeInfo (地址、容量、状态)
```

### 2.4 Subtree Partitioning（元数据水平扩展）

```
目录树分区示例：

/                        ← Raft Group 0 (根组)
├── /dataset/            ← Raft Group 1
│   ├── /dataset/imagenet/
│   └── /dataset/coco/
├── /models/             ← Raft Group 2
│   ├── /models/llama/
│   └── /models/gpt/
└── /checkpoints/        ← Raft Group 3

分区路由表（存在所有 MetaNode 中）：
  "/"             → Raft Group 0, Leader: MetaNode-1
  "/dataset/"     → Raft Group 1, Leader: MetaNode-2
  "/models/"      → Raft Group 2, Leader: MetaNode-3
  "/checkpoints/" → Raft Group 3, Leader: MetaNode-1

Client 根据路径前缀匹配，定位到正确的 Raft Group。
```

---

## 三、DataNode 详细设计

### 3.1 进程架构

```
DataNode 进程
├── RPC Server (gRPC/brpc)
│   ├── BlockReadService    ← 处理 Block 读取请求
│   ├── BlockWriteService   ← 处理 Block 写入请求
│   └── InternalService     ← 节点间数据迁移/修复
├── SegmentEngine
│   ├── SegmentWriter       ← Segment 追加写入器
│   ├── SegmentReader       ← Segment 读取器
│   ├── SegmentSealer       ← Segment 封存管理
│   └── SegmentIndex        ← 本地 Block 索引
├── IOEngine
│   ├── IoUringWorker       ← io_uring 异步 IO
│   └── BufferPool          ← 内存缓冲池 & 零拷贝
├── IntegrityChecker
│   ├── CRCValidator        ← 读写时 CRC 校验
│   └── BackgroundScrubber  ← 后台定期巡检
└── HeartbeatReporter
    ├── MetaReporter        ← 向 MetaCluster 上报心跳
    └── DiskMonitor         ← 磁盘健康 & 使用率监控
```

### 3.2 Segment 存储结构

```
磁盘布局：

/data/openfs/
├── segment_000001.dat      ← 256MB, sealed (只读)
├── segment_000002.dat      ← 256MB, sealed
├── segment_000003.dat      ← 活跃 Segment (追加写入中)
├── segment_index.db        ← LevelDB/RocksDB 本地索引
└── node.conf               ← 节点配置

Segment 文件内部结构：

┌────────────────────────────────────────────────────┐
│ Segment Header (4KB)                                │
│   magic: "OFSSG001"                                 │
│   segment_id: uint64                                │
│   create_time: timestamp                            │
│   status: ACTIVE | SEALED                           │
│   block_count: uint32                               │
│   used_bytes: uint64                                │
├────────────────────────────────────────────────────┤
│ Block Entry 1                                       │
│   ┌─────────────────────────────────────────────┐  │
│   │ BlockHeader (64B)                            │  │
│   │   block_id: uint64                           │  │
│   │   level: uint8                               │  │
│   │   data_size: uint32                          │  │
│   │   crc32: uint32                              │  │
│   │   flags: uint16                              │  │
│   ├─────────────────────────────────────────────┤  │
│   │ Block Data (变长, 对齐到 4KB)                 │  │
│   └─────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────┤
│ Block Entry 2                                       │
│   ┌─────────────────────────────────────────────┐  │
│   │ BlockHeader (64B)                            │  │
│   ├─────────────────────────────────────────────┤  │
│   │ Block Data                                   │  │
│   └─────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────┤
│ ...                                                 │
├────────────────────────────────────────────────────┤
│ Segment Footer (4KB)  ← seal 时写入                 │
│   block_index: [(block_id, offset, size), ...]     │
│   total_crc: uint32                                 │
└────────────────────────────────────────────────────┘
```

### 3.3 PackedBlock 内部结构

```
PackedBlock (物理大小对齐到 4MB):

┌──────────────────────────────────────────┐
│ PackHeader (64B)                          │
│   pack_block_id: uint64                   │
│   entry_count: uint32                     │
│   total_data_size: uint32                 │
├──────────────────────────────────────────┤
│ Entry[0]: [inode_id | size | data...]    │
│ Entry[1]: [inode_id | size | data...]    │
│ Entry[2]: [inode_id | size | data...]    │
│ ...                                       │
│ Entry[N]: [inode_id | size | data...]    │
├──────────────────────────────────────────┤
│ Padding (对齐到 4KB 边界)                 │
├──────────────────────────────────────────┤
│ PackIndex (尾部索引)                      │
│   [(inode_id, offset, size), ...]        │
└──────────────────────────────────────────┘
```

---

## 四、Client 详细设计

### 4.1 libclient 架构

```
libclient
├── ConnectionManager
│   ├── MetaConnection     ← 到 MetaCluster 的连接（带路由缓存）
│   └── DataConnection     ← 到 DataNode 的连接池
├── MetadataCache
│   ├── InodeCache         ← inode 信息缓存
│   ├── DentryCache        ← 目录项缓存
│   └── BlockLocationCache ← Block 位置缓存
├── IOPipeline
│   ├── WritePipeline      ← 写入管线
│   │   ├── BlockSplitter  ← 文件切分为 Block（自动选级别）
│   │   ├── BatchBuffer    ← 批次缓冲（Append-Batched-Striping）
│   │   ├── CRCCalculator  ← CRC32 计算
│   │   └── AsyncSender    ← 异步发送到 DataNode
│   └── ReadPipeline       ← 读取管线
│       ├── Prefetcher     ← 预读 & Pipeline 并行拉取
│       ├── CRCVerifier    ← CRC32 校验
│       └── ReadCache      ← 客户端侧读缓存
└── InterfaceAdaptor
    ├── PosixAdaptor       ← POSIX 语义适配
    └── S3Adaptor          ← S3 语义适配
```

### 4.2 写入流程（详细）

```
Client 写入一个 50MB 文件：

1. Client → MetaNode: CreateFile("/dataset/train/img_0001.tar")
   ← MetaNode 返回: inode_id=10086, block_level=L3(32MB)

2. Client 将 50MB 文件切分:
   Block-0: 32MB (L3)
   Block-1: 18MB (L3, 尾部 Block 不足 32MB 也用 L3)

3. Client → MetaNode: AllocateBlocks(inode=10086, count=2)
   ← MetaNode 根据负载选择目标节点:
     Block-0 → DataNode-5, Segment-003
     Block-1 → DataNode-5, Segment-003  (同批次同节点，保证顺序写)

4. Client → DataNode-5: WriteBlock(block_id=xxx, data=32MB, crc32=yyy)
   DataNode-5 追加写入 Segment-003, 校验 CRC32
   ← DataNode-5: WriteOK, offset=128MB

5. Client → DataNode-5: WriteBlock(block_id=xxx, data=18MB, crc32=yyy)
   DataNode-5 继续追加写入 Segment-003
   ← DataNode-5: WriteOK, offset=160MB

6. Client → MetaNode: CommitBlocks(inode=10086, blocks=[...位置信息...])
   MetaNode 持久化 Block 映射关系 (Raft 提交)
   ← MetaNode: CommitOK
```

### 4.3 读取流程（详细）

```
Client 读取文件 "/dataset/train/img_0001.tar":

1. Client 查本地 MetadataCache
   ← Cache Miss

2. Client → MetaNode: Lookup("/dataset/train/img_0001.tar")
   ← MetaNode 返回: inode_id=10086, size=50MB, block_level=L3
     blocks: [
       {block_id=A, node=DN-5, seg=003, offset=128MB, size=32MB, crc=xxx},
       {block_id=B, node=DN-5, seg=003, offset=160MB, size=18MB, crc=yyy}
     ]

3. Client 缓存元数据到本地 Cache

4. Client → DataNode-5: ReadBlock(seg=003, offset=128MB, size=32MB)
   (同时) Client → DataNode-5: ReadBlock(seg=003, offset=160MB, size=18MB)
   ← Pipeline 并行读取, DataNode 内部连续读取（物理顺序）

5. Client 校验每个 Block 的 CRC32
   ← 校验通过, 拼装返回给应用层
   ← 校验失败, 上报 MetaNode 触发修复
```

---

## 五、核心流程设计

### 5.1 DataNode 注册与发现

```
1. DataNode 启动 → 读取配置获取 MetaCluster 地址
2. DataNode → MetaNode: Register(node_id, addr, disk_info)
3. MetaNode 记录节点信息, 分配 node_id（如首次注册）
4. MetaNode 将新节点加入调度池
5. 如需 Rebalance, MetaNode 后台开始迁移 Block 到新节点
```

### 5.2 心跳与故障检测

```
周期: 每 3 秒

DataNode → MetaNode: Heartbeat {
    node_id,
    timestamp,
    disk_usage: [(disk_path, total, used, io_util)],
    segment_count,
    block_count,
    active_connections,
    cpu_load
}

MetaNode 侧:
- 收到心跳 → 更新节点状态 + 调度权重
- 超过 10s 未收到 → 标记 SUSPECT
- 超过 30s 未收到 → 标记 OFFLINE, 触发修复流程
```

### 5.3 Block 修复流程

```
MetaNode 检测到 DataNode-5 OFFLINE:

1. MetaNode 查询 BlockMetaTable, 找到所有 node_id=5 的 Block
   → 假设有 100 万个 Block 受影响

2. 对每个受影响的 Block:
   a. 检查是否有其他副本 → 如有, 从副本复制到新节点
   b. 如无副本(单副本模式), 标记为 LOST, 等待节点恢复
   c. 如节点永久下线, 通知上层应用数据丢失

3. 修复调度:
   - 将修复任务分散到所有存活 DataNode (并行修复)
   - 优先修复热 Block (访问频次高的)
   - 限流: 修复流量不超过各节点带宽的 30%, 避免影响在线业务

4. DataNode-5 恢复上线:
   - 重新注册, 上报 Block 清单
   - MetaNode 对比, 恢复正常调度
```

### 5.4 Rebalance 流程

```
触发条件: 新节点加入 / 节点间使用率偏差 > 20%

1. MetaNode 计算全局 Block 分布
   目标: 各节点磁盘使用率偏差 < 5%

2. 生成迁移计划:
   Source: 高使用率节点的 sealed Segment 中的 Block
   Target: 低使用率节点

3. 执行迁移:
   MetaNode → Source DataNode: ReadBlock(block_id)
   MetaNode → Target DataNode: WriteBlock(block_id, data)
   MetaNode: 更新 BlockMeta 中的 node_id
   (实际由 DataNode 之间直接传输, MetaNode 只协调)

4. 迁移限流: 不超过各节点带宽的 20%
```

### 5.5 热度感知副本调度

```
MetaNode 周期性任务 (每 5 分钟):

1. 扫描 BlockMeta 中 access_count 变化
2. 热 Block (access_count > 阈值):
   - 当前副本数 < 2 → 选择就近节点复制一个副本
   - 更新 BlockMeta.replica_count = 2
3. 冷 Block (access_count < 阈值且 > 1 副本):
   - 降为单副本, 回收多余空间
   - 更新 BlockMeta.replica_count = 1
```

---

## 六、RPC 接口定义

### 6.1 Client ↔ MetaNode

```protobuf
service MetaService {
    // 文件操作
    rpc CreateFile(CreateFileReq)       returns (CreateFileResp);
    rpc DeleteFile(DeleteFileReq)       returns (DeleteFileResp);
    rpc GetFileInfo(GetFileInfoReq)     returns (GetFileInfoResp);
    rpc Rename(RenameReq)               returns (RenameResp);

    // 目录操作
    rpc MkDir(MkDirReq)                 returns (MkDirResp);
    rpc ReadDir(ReadDirReq)             returns (ReadDirResp);
    rpc RmDir(RmDirReq)                 returns (RmDirResp);

    // Block 分配与提交
    rpc AllocateBlocks(AllocBlocksReq)  returns (AllocBlocksResp);
    rpc CommitBlocks(CommitBlocksReq)   returns (CommitBlocksResp);
    rpc GetBlockLocations(GetBlockLocsReq) returns (GetBlockLocsResp);
}
```

### 6.2 Client ↔ DataNode

```protobuf
service DataService {
    rpc WriteBlock(WriteBlockReq)       returns (WriteBlockResp);
    rpc ReadBlock(ReadBlockReq)         returns (ReadBlockResp);
    rpc DeleteBlock(DeleteBlockReq)     returns (DeleteBlockResp);

    // 流式接口 (大 Block)
    rpc WriteBlockStream(stream WriteChunk) returns (WriteBlockResp);
    rpc ReadBlockStream(ReadBlockReq)   returns (stream ReadChunk);
}
```

### 6.3 DataNode ↔ MetaNode

```protobuf
service NodeService {
    rpc Register(RegisterReq)           returns (RegisterResp);
    rpc Heartbeat(HeartbeatReq)         returns (HeartbeatResp);
    rpc ReportBlocks(ReportBlocksReq)   returns (ReportBlocksResp);
}
```

### 6.4 DataNode ↔ DataNode

```protobuf
service TransferService {
    rpc TransferBlock(TransferReq)      returns (TransferResp);
    rpc TransferBlockStream(stream TransferChunk) returns (TransferResp);
}
```

---

## 七、多级缓存架构设计

### 7.1 设计理念

缓存是独立于存储的加速层，**不承担数据持久化职责**，一致性由元数据驱动保证。
核心原则：

- **缓存即加速，非存储**：缓存丢失不影响数据安全，仅影响性能
- **硬件感知**：自动识别 DRAM / NVMe / HDD 并分配角色
- **元数据驱动一致性**：缓存的有效性由元数据的版本号裁决，无需复杂的缓存一致性协议
- **可插拔扩展**：缓存层可独立扩缩容，不影响存储集群

### 7.2 三级缓存体系

```
                    热度递减 / 容量递增
                    ─────────────────→

┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│   L1 Cache   │  │   L2 Cache   │  │   L3 Cache   │  │  Cold Store  │
│   Client     │  │  DataNode    │  │  DataNode    │  │  DataNode    │
│   DRAM       │  │  DRAM        │  │  NVMe        │  │  HDD         │
│              │  │              │  │              │  │              │
│  延迟: <1μs  │  │ 延迟: ~10μs  │  │ 延迟: ~100μs │  │ 延迟: ~5ms   │
│  容量: GB级   │  │ 容量: 10GB级  │  │ 容量: TB级   │  │ 容量: 10TB+  │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │ miss            │ miss            │ miss            │
       └────────→────────└────────→────────└────────→────────┘
                              回填 (promote)
       ←─────────────────←─────────────────←─────────────────┘
```

### 7.3 各级缓存详细设计

#### L1：Client 侧 DRAM 缓存

```
Client 进程内：

L1Cache
├── MetadataCache          ← 元数据缓存（最重要）
│   ├── InodeCache         ← inode 信息, LRU, 容量 ~100K 条目
│   ├── DentryCache        ← 目录项, LRU, 容量 ~500K 条目
│   └── BlockLocationCache ← Block 位置映射, LRU, 容量 ~1M 条目
├── DataCache              ← 数据缓存
│   ├── ReadCache          ← 最近读取的 Block 数据, LRU, 可配置 256MB-4GB
│   └── WriteBuffer        ← 写入缓冲（BatchBuffer 的一部分）
└── PrefetchBuffer         ← 预读缓冲, 顺序读场景下提前拉取下一批 Block
```

**一致性机制：**
```
每个缓存条目携带版本号 (version):

MetaCache Entry: { inode_id, data, version, expire_time }
DataCache Entry: { block_id, data, version, crc32 }

读取流程：
1. 查 L1 Cache → 命中 → 检查 version
2. 如 expire_time 未过期 → 直接返回（不校验远端）
3. 如已过期 → 向 MetaNode 发 CheckVersion(inode_id, local_version)
   ← MetaNode 返回: VALID（版本相同）或 UPDATED(新数据)
4. VALID → 刷新 expire_time，返回缓存数据
5. UPDATED → 更新缓存，返回新数据

写入流程：
1. 写入成功后 → 本地 L1 Cache 立即更新 + version++
2. MetaNode 广播 Invalidate(inode_id, new_version) 到其他 Client
3. 其他 Client 收到后标记该条目过期
```

#### L2：DataNode DRAM 缓存

```
DataNode 进程内：

L2Cache (DRAM)
├── HotBlockCache          ← 热 Block 数据缓存
│   ├── 容量: 物理内存的 30-50%（可配置）
│   ├── 淘汰策略: LRU-2（两次访问才提升优先级，防止扫描污染）
│   └── 粒度: 整个 Block（4KB-256MB）
├── SegmentIndexCache      ← Segment 索引常驻内存
└── BloomFilter            ← Block 存在性快速判定（避免无效磁盘查找）
```

**工作原理：**
```
Client ReadBlock 请求到达 DataNode：

1. 查 L2 DRAM Cache → 命中 → 直接返回（零磁盘 IO）
2. 未命中 → 查 L3 NVMe Cache
3. L3 也未命中 → 从 HDD Segment 读取
4. 读取后回填 L2（如果 Block 是第二次被访问）
```

#### L3：DataNode NVMe 缓存层

```
DataNode NVMe 缓存：

NVMeCacheEngine
├── CacheStore             ← NVMe 上的缓存数据文件
│   ├── cache_segment_001.dat   ← 类似 Segment 格式，顺序写入
│   ├── cache_segment_002.dat
│   └── ...
├── CacheIndex             ← 缓存索引 (BlockID → NVMe offset)
│   └── 存在 NVMe 上的轻量 RocksDB 实例
├── AdmissionPolicy        ← 准入策略（什么数据值得缓存到 NVMe）
└── EvictionPolicy         ← 淘汰策略（NVMe 空间不足时的驱逐）
```

**NVMe 缓存的典型硬件架构：**
```
DataNode 硬件配置示例：

配置 A（性价比型）：
  DRAM: 64GB (L2: 32GB)
  NVMe: 2TB  (L3 缓存)
  HDD:  4×12TB (Cold Store, 数据 Segment)

配置 B（高性能型）：
  DRAM: 256GB (L2: 128GB)
  NVMe: 4×4TB (L3 缓存，大容量)
  HDD:  8×16TB (Cold Store)

配置 C（全闪存型）：
  DRAM: 256GB (L2: 128GB)
  NVMe: 8×4TB (既是缓存也是存储，无 HDD)
```

**准入策略（Admission Policy）：**
```
不是所有数据都值得写入 NVMe 缓存：

准入条件（满足任一即可）：
1. 热度准入：Block 被访问 ≥ 2 次（避免单次扫描污染）
2. 关联准入：同一文件的相邻 Block 已在缓存中（预判顺序读）
3. 优先准入：Block Level ≤ L1（小文件/PackedBlock 优先缓存，收益最大）
4. AI 场景准入：标记为 training_data 的文件自动预热到 NVMe

拒绝条件：
1. Block 正在被 Rebalance 迁移（临时数据不缓存）
2. Block 的 TTL < 阈值（即将过期/删除的不缓存）
```

**淘汰策略（Eviction Policy）：**
```
NVMe 使用率 > 90% 时触发淘汰：

淘汰优先级（从高到低，优先淘汰）：
1. 已被删除但缓存还在的 Block（元数据标记已删除）
2. 最久未访问的 L3/L4 大 Block（大块腾出空间快）
3. 冷 Block（热度跟踪值最低的）
4. 有多副本的 Block（其他节点也有，淘汰无风险）
```

### 7.4 元数据驱动缓存一致性

```
核心机制：Version + Lease（版本号 + 租约）

┌────────────────────────────────────────────────────────┐
│                    MetaNode (权威源)                     │
│                                                         │
│  每个 Inode/Block 维护:                                  │
│    version: uint64    ← 每次修改 +1                      │
│    lease_holders: []  ← 当前持有缓存租约的 Client 列表    │
│    lease_duration: 30s ← 租约有效期                       │
└───────────┬─────────────────────────────────────────────┘
            │
     ┌──────┴──────┐
     ▼              ▼
┌─────────┐   ┌─────────┐
│Client A │   │Client B │
│ v=5     │   │ v=5     │     ← 两个 Client 都缓存了 version 5
└─────────┘   └─────────┘

场景：Client A 修改文件

1. Client A → MetaNode: UpdateFile(inode=X, new_data)
2. MetaNode: version 5 → 6, 持久化
3. MetaNode → Client B: Invalidate(inode=X, new_version=6)
   (通过 Client B 的长连接推送, 或下次 Client B 请求时携带)
4. Client B 收到后: 标记本地 inode=X 缓存失效
5. Client B 下次读取: 重新从 MetaNode 拉取 version 6

极端情况处理：
- MetaNode 推送失败 → Client 的 lease 30s 后自然过期 → 下次访问会校验版本
- Client 崩溃重启 → 缓存全部丢弃，重新拉取
- 网络分区 → lease 过期后 Client 无法读取缓存，必须重连 MetaNode
```

**DataNode 缓存一致性（更简单）：**
```
DataNode 缓存不需要复杂一致性协议：

1. Block 一旦写入 Segment 就不可变（immutable）
2. 缓存的 Block 数据永远有效（只要 Block 没被删除）
3. 删除时：MetaNode 通知 DataNode 删除 Block
   → DataNode 同时清除 L2/L3 缓存中的对应条目
4. 不存在"修改已有 Block"的场景 → 无缓存不一致问题

这就是 immutable Block 设计的巨大优势：
  写入不可变 → 缓存天然一致 → 无需失效协议
```

### 7.5 写入缓存与数据一致性

```
写入路径的缓存设计（Write-Through + Write-Back 混合）：

模式 1: Write-Through（默认，强一致）
  Client 写入 → Block 数据发送到 DataNode → DataNode 写入 Segment（持久化）
  → 返回成功 → Client 确认
  特点：每次写入都落盘，数据不会丢失
  适用：关键数据（模型 checkpoint、配置文件）

模式 2: Write-Back（可选，高性能）
  Client 写入 → Block 数据发送到 DataNode → DataNode 写入 NVMe 缓存
  → 返回成功 → 后台异步刷入 HDD Segment
  特点：写入延迟低（NVMe 速度），但断电可能丢失未刷盘数据
  适用：可再生数据（训练中间结果、临时文件）
  安全保障：
    - NVMe 缓存中的数据有 WAL（Write-Ahead Log）保护
    - 定期 flush（每 5s 或累计 256MB）
    - 元数据标记该 Block 为 CACHED_ONLY 状态，完全落盘后改为 PERSISTED

模式选择由元数据驱动：
  MetaNode 根据文件路径/标签自动选择：
    /checkpoints/*  → Write-Through
    /tmp/*          → Write-Back
    /dataset/*      → Write-Through
    用户也可在 Client SDK 中显式指定
```

### 7.6 缓存预热

```
AI 场景的缓存预热策略：

1. 主动预热（用户触发）：
   openfs-cli cache warmup /dataset/imagenet/
   → MetaNode 获取该目录下所有 Block 列表
   → 调度各 DataNode 将 HDD Block 加载到 NVMe/DRAM 缓存
   → 训练任务启动前执行，避免冷启动

2. 智能预热（系统自动）：
   HeatTracker 发现某目录访问模式为顺序扫描
   → 自动预读后续 Block 到 NVMe 缓存
   → 训练 epoch 2 开始时数据已全部在缓存中

3. 跨节点预热：
   训练任务从 Node-A 迁移到 Node-B
   → MetaNode 感知任务迁移
   → 指令 Node-B 预热 Node-A 上的热 Block
   → 任务在 Node-B 启动时缓存已就绪
```

### 7.7 硬件配置适配

```
系统启动时自动探测硬件，生成缓存分层策略：

硬件探测：
  DataNode 启动 → 扫描所有存储设备
  → 识别类型：DRAM / NVMe / SATA SSD / HDD
  → 测量基准性能（顺序读写带宽、随机 IOPS、延迟）
  → 上报 MetaNode

自动角色分配：

┌─────────────────────────────────────────────────┐
│            硬件                →     角色         │
├─────────────────────────────────────────────────┤
│ DRAM                          → L2 Cache         │
│ NVMe (容量 ≤ HDD 总量 20%)    → L3 Cache         │
│ NVMe (无 HDD 的全闪存配置)     → L3 Cache + Store │
│ SATA SSD                      → L3 Cache 或 Store│
│ HDD                           → Cold Store       │
└─────────────────────────────────────────────────┘

混合部署示例：

集群 A（冷热分层）：
  10 台 HDD 节点（每台 4×12TB HDD + 1×2TB NVMe 做缓存）
  → NVMe 做 L3 缓存，HDD 做存储
  → 热数据全在 NVMe 上，读延迟 ~100μs
  → 冷数据在 HDD 上，读延迟 ~5ms

集群 B（全闪存高性能）：
  5 台全 NVMe 节点（每台 8×4TB NVMe）
  → NVMe 同时做缓存和存储，无 HDD
  → 所有数据读延迟 ~100μs

集群 C（混合集群）：
  3 台 NVMe 节点 + 10 台 HDD 节点
  → NVMe 节点优先接收热 Block
  → HDD 节点存储冷 Block
  → 元数据根据热度自动迁移 Block 在 NVMe 节点和 HDD 节点之间
```

### 7.8 缓存相关的元数据扩展

```cpp
// Block 元数据扩展
struct BlockMeta {
    // ... 原有字段 ...
    
    // 缓存相关
    CacheState  cache_state;    // UNCACHED / L3_CACHED / L2_CACHED
    uint64_t    last_access;    // 最近访问时间戳
    uint32_t    access_count;   // 访问计数
    WriteMode   write_mode;     // WRITE_THROUGH / WRITE_BACK
    PersistState persist_state; // CACHED_ONLY / PERSISTED
};

// DataNode 上报的存储设备信息
struct StorageDevice {
    string      path;           // 设备挂载路径
    DeviceType  type;           // NVME / SATA_SSD / HDD
    uint64_t    capacity;       // 总容量
    uint64_t    used;           // 已用容量
    uint32_t    seq_read_mbps;  // 顺序读带宽 (MB/s)
    uint32_t    rand_iops;      // 随机 IOPS
    uint32_t    avg_latency_us; // 平均延迟 (μs)
};

// DataNode 心跳扩展
struct HeartbeatReq {
    // ... 原有字段 ...
    
    // 缓存状态
    uint64_t    l2_cache_size;      // L2 DRAM 缓存大小
    uint64_t    l2_cache_used;      // L2 已用
    float       l2_hit_rate;        // L2 命中率
    uint64_t    l3_cache_size;      // L3 NVMe 缓存大小
    uint64_t    l3_cache_used;      // L3 已用
    float       l3_hit_rate;        // L3 命中率
    vector<StorageDevice> devices;  // 存储设备列表
};
```

---

## 八、技术栈

| 组件 | 技术选型 | 用途 |
|---|---|---|
| RPC 框架 | brpc | 高性能节点间通信 |
| 共识协议 | braft | Raft 实现（元数据一致性） |
| 元数据存储 | RocksDB | MetaNode 本地 KV 持久化 |
| 本地索引 | RocksDB | DataNode 本地 Block 索引 |
| 异步 IO | io_uring (liburing) | DataNode 磁盘 IO |
| 序列化 | protobuf | RPC 消息 & 数据结构序列化 |
| FUSE | libfuse3 | POSIX 文件系统挂载 |
| HTTP | brpc 内置 HTTP | S3 Gateway |
| 构建 | CMake | 项目构建 |
| 包管理 | vcpkg / 系统包 | 第三方依赖管理 |
| 测试 | Google Test | 单元测试 & 集成测试 |
| 日志 | spdlog | 结构化日志 |

---

## 九、项目目录结构

```
openfs/
├── CMakeLists.txt
├── cmake/                      ← CMake 模块 & 第三方依赖
│   ├── FindBraft.cmake
│   └── dependencies.cmake
├── proto/                      ← Protobuf 定义
│   ├── meta_service.proto
│   ├── data_service.proto
│   ├── node_service.proto
│   └── common.proto
├── src/
│   ├── common/                 ← 公共基础库
│   │   ├── config.h/cpp        ← 配置管理
│   │   ├── types.h             ← 公共类型定义
│   │   ├── crc32.h/cpp         ← CRC32 计算
│   │   ├── id_generator.h/cpp  ← 全局 ID 生成
│   │   └── logging.h           ← 日志封装
│   ├── meta/                   ← MetaNode
│   │   ├── meta_server.h/cpp           ← MetaNode 主服务
│   │   ├── raft_state_machine.h/cpp    ← Raft 状态机
│   │   ├── namespace_manager.h/cpp     ← 命名空间 & 目录树
│   │   ├── inode_table.h/cpp           ← inode 管理
│   │   ├── block_map.h/cpp             ← Block 映射管理
│   │   ├── block_allocator.h/cpp       ← Block 写入分配
│   │   ├── node_manager.h/cpp          ← DataNode 管理
│   │   ├── repair_manager.h/cpp        ← Block 修复调度
│   │   ├── rebalance_manager.h/cpp     ← Rebalance 调度
│   │   └── heat_tracker.h/cpp          ← 热度跟踪
│   ├── data/                   ← DataNode
│   │   ├── data_server.h/cpp           ← DataNode 主服务
│   │   ├── segment_engine.h/cpp        ← Segment 存储引擎
│   │   ├── segment_writer.h/cpp        ← Segment 追加写入
│   │   ├── segment_reader.h/cpp        ← Segment 读取
│   │   ├── segment_sealer.h/cpp        ← Segment 封存
│   │   ├── packed_block.h/cpp          ← PackedBlock 打包/解包
│   │   ├── io_engine.h/cpp             ← io_uring 异步 IO
│   │   ├── integrity_checker.h/cpp     ← 数据完整性校验
│   │   ├── heartbeat_reporter.h/cpp    ← 心跳上报
│   │   ├── l2_cache.h/cpp              ← L2 DRAM 缓存 (LRU-2)
│   │   └── nvme_cache_engine.h/cpp     ← L3 NVMe 缓存引擎
│   ├── client/                 ← Client SDK
│   │   ├── openfs_client.h/cpp         ← Client 主接口
│   │   ├── meta_connection.h/cpp       ← MetaCluster 连接
│   │   ├── data_connection.h/cpp       ← DataNode 连接池
│   │   ├── metadata_cache.h/cpp        ← 元数据缓存 (L1)
│   │   ├── data_cache.h/cpp            ← 数据读缓存 (L1)
│   │   ├── write_pipeline.h/cpp        ← 写入管线
│   │   ├── read_pipeline.h/cpp         ← 读取管线
│   │   ├── block_splitter.h/cpp        ← 文件切分 & Block 级别选择
│   │   └── batch_buffer.h/cpp          ← 批次缓冲
│   ├── fuse/                   ← FUSE 文件系统
│   │   └── openfs_fuse.h/cpp           ← FUSE 适配层
│   ├── s3/                     ← S3 Gateway
│   │   ├── s3_server.h/cpp             ← S3 HTTP 服务
│   │   └── s3_handler.h/cpp            ← S3 请求处理
│   └── cli/                    ← 命令行工具
│       └── openfs_cli.cpp              ← CLI 主程序
├── tests/                      ← 测试
│   ├── unit/
│   │   ├── test_crc32.cpp
│   │   ├── test_segment_engine.cpp
│   │   ├── test_block_allocator.cpp
│   │   └── ...
│   └── integration/
│       ├── test_single_node.cpp
│       ├── test_cluster.cpp
│       └── ...
├── conf/                       ← 配置文件模板
│   ├── metanode.conf
│   ├── datanode.conf
│   └── client.conf
├── scripts/                    ← 运维脚本
│   ├── start_meta.sh
│   ├── start_data.sh
│   └── deploy.sh
└── docs/                       ← 文档
    ├── requirements.md
    ├── architecture.md
    └── development-plan.md
```