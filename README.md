# OpenFS - AI场景下的高性能分布式存储系统

OpenFS是一个专为AI场景设计的高性能分布式存储系统，采用元数据驱动的安全架构，实现90%+的存储利用率。

## 核心特性

- **Block级调度**：所有数据打散为统一Block，无固有归属
- **元数据驱动安全**：不依赖数据冗余，靠元数据校验+快速重建
- **顺序写入+延迟分散**：节点内Segment顺序追加，节点间批次轮转
- **多级缓存**：L1 Client DRAM → L2 DataNode DRAM → L3 NVMe → HDD
- **硬件感知**：自动适配NVMe/HDD混合部署

## 架构优势

### 存储利用率提升
- 传统架构：33-75%利用率
- OpenFS架构：90%+利用率

### 性能优化
- 顺序写入最大化磁盘吞吐
- 多级缓存降低延迟
- 智能Block调度平衡负载

### 可靠性保障
- 元数据驱动的快速重建
- Block级精细管理
- 自动故障检测与恢复

## 系统架构

```
┌─────────────────┐    ┌─────────────────┐
│   MetaNode      │    │   MetaNode      │
│  (元数据管理)    │    │  (元数据管理)    │
└─────────────────┘    └─────────────────┘
         ▲                       ▲
         │                       │
         ▼                       ▼
┌─────────────────┐    ┌─────────────────┐
│   DataNode      │    │   DataNode      │
│  (数据存储)      │    │  (数据存储)      │
└─────────────────┘    └─────────────────┘
         ▲                       ▲
         │                       │
         └───────────────────────┘
                Block Storage
```

## 快速开始

### 环境要求
- C++17兼容编译器
- CMake 3.16+
- Protocol Buffers
- gRPC

### 构建项目

```bash
# 克隆项目
git clone <repository-url>
cd openfs

# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make -j$(nproc)
```

### 启动服务

#### 启动MetaNode
```bash
./meta_node ../configs/meta_node.conf
```

#### 启动DataNode
```bash
./data_node ../configs/data_node.conf
```

## 配置说明

### MetaNode配置 (configs/meta_node.conf)
```
node_id=1
listen_host=0.0.0.0
listen_port=50050
inode_table_path=/tmp/openfs/inode_table.dat
namespace_path=/tmp/openfs/namespace.dat
log_level=INFO
```

### DataNode配置 (configs/data_node.conf)
```
node_id=1
data_dir=/tmp/openfs/data
segment_size_mb=1024
listen_host=0.0.0.0
listen_port=50051
log_level=INFO
```

## 开发计划

### 第一阶段：核心架构实现
- ✅ CMake项目搭建
- ✅ Protobuf接口定义
- ✅ 基础库实现
- ✅ MetaNode核心组件
- ✅ DataNode核心组件

### 第二阶段：高级功能
- [ ] 客户端SDK开发
- [ ] Block级别自动管理
- [ ] 多级缓存系统
- [ ] 性能优化

### 第三阶段：生产就绪
- [ ] 监控告警系统
- [ ] 安全配置
- [ ] 运维工具
- [ ] 文档完善

## 技术文档

- [架构设计](docs/architecture.md)
- [需求文档](docs/requirements.md)
- [开发计划](docs/development-plan.md)

## 贡献指南

欢迎提交Issue和Pull Request来改进OpenFS！

## 许可证

MIT License