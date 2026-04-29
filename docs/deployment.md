# OpenFS 生产部署文档

## 一、系统要求

### 1.1 操作系统

| 发行版 | 最低版本 | 推荐版本 |
|--------|----------|----------|
| RHEL / CentOS / Rocky / AlmaLinux | 8.0 | 9.x |
| Ubuntu | 20.04 LTS | 22.04 / 24.04 LTS |

> 内核要求：Linux 5.4+（推荐 5.10+ 以支持 io_uring 优化）

### 1.2 硬件要求

| 角色 | CPU | 内存 | 磁盘 | 网络 |
|------|-----|------|------|------|
| MetaNode | ≥ 8 核 | ≥ 32GB | SSD 200GB+（元数据） | 10Gbps |
| DataNode | ≥ 16 核 | ≥ 64GB | NVMe 2TB+（数据盘） | 10Gbps |
| 压测客户端 | ≥ 8 核 | ≥ 16GB | SSD 100GB | 10Gbps |

### 1.3 软件依赖

| 依赖 | 最低版本 | 用途 |
|------|----------|------|
| GCC / Clang | GCC 9+ / Clang 13+ | C++17 编译 |
| CMake | 3.16+ | 构建系统 |
| vcpkg | latest | C++ 依赖管理 |
| protobuf | 3.21+ | 序列化 |
| gRPC | 1.50+ | RPC 框架 |
| spdlog | 1.10+ | 日志 |
| GTest | 1.12+ | 单元测试（可选） |

---

## 二、编译构建

### 2.1 RHEL / CentOS / Rocky / AlmaLinux

```bash
# ---- 安装基础工具 ----
# RHEL 8/9
sudo dnf install -y gcc gcc-c++ make cmake git wget tar which

# CentOS 7（如需支持）
sudo yum install -y centos-release-scl
sudo yum install -y devtoolset-11 cmake3 git wget tar
scl enable devtoolset-11 bash
# 将 cmake3 链接为 cmake
sudo alternatives --install /usr/bin/cmake cmake /usr/bin/cmake3 1

# ---- 安装 vcpkg ----
sudo git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
sudo /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'export VCPKG_ROOT=/opt/vcpkg' | sudo tee /etc/profile.d/vcpkg.sh
echo 'export PATH=$VCPKG_ROOT:$PATH' | sudo tee -a /etc/profile.d/vcpkg.sh
source /etc/profile.d/vcpkg.sh

# ---- 安装 C++ 依赖 ----
vcpkg install grpc protobuf spdlog gtest --triplet=x64-linux

# ---- 编译 OpenFS ----
cd /opt/openfs
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)

# ---- 验证产物 ----
ls -lh build/meta_node build/data_node
```

### 2.2 Ubuntu / Debian

```bash
# ---- 安装基础工具 ----
sudo apt update
sudo apt install -y build-essential cmake git ninja-build wget pkg-config

# ---- 安装 vcpkg ----
sudo git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
sudo /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'export VCPKG_ROOT=/opt/vcpkg' | sudo tee /etc/profile.d/vcpkg.sh
echo 'export PATH=$VCPKG_ROOT:$PATH' | sudo tee -a /etc/profile.d/vcpkg.sh
source /etc/profile.d/vcpkg.sh

# ---- 安装 C++ 依赖 ----
vcpkg install grpc protobuf spdlog gtest --triplet=x64-linux

# ---- 编译 OpenFS ----
cd /opt/openfs
cmake -B build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)

# ---- 验证产物 ----
ls -lh build/meta_node build/data_node
```

### 2.3 离线构建（生产推荐）

在构建机上编译完成后，打包部署到生产节点，无需在生产环境安装编译工具链。

```bash
# ---- 构建机：打包 ----
mkdir -p openfs-dist/{bin,etc,lib}
cp build/meta_node build/data_node openfs-dist/bin/
cp configs/meta_node.conf configs/data_node.conf openfs-dist/etc/

# 收集运行时动态库依赖
ldd build/meta_node | grep "=> /" | awk '{print $3}' | xargs -I '{}' cp -v '{}' openfs-dist/lib/
ldd build/data_node | grep "=> /" | awk '{print $3}' | xargs -I '{}' cp -v '{}' openfs-dist/lib/

# 打 vcpkg 安装的库
cp -r $VCPKG_ROOT/installed/x64-linux/lib/*.so* openfs-dist/lib/ 2>/dev/null || true

tar czf openfs-1.0.0-linux-x86_64.tar.gz openfs-dist/
```

```bash
# ---- 生产节点：部署 ----
sudo tar xzf openfs-1.0.0-linux-x86_64.tar.gz -C /opt/
sudo ln -sf /opt/openfs-dist/lib/*.so* /usr/local/lib/
sudo ldconfig

# 添加 PATH
echo 'export PATH=/opt/openfs-dist/bin:$PATH' | sudo tee /etc/profile.d/openfs.sh
source /etc/profile.d/openfs.sh
```

---

## 三、目录规划

```
/opt/openfs/
├── bin/
│   ├── meta_node               # MetaNode 可执行文件
│   └── data_node               # DataNode 可执行文件
├── etc/
│   ├── meta_node.conf          # MetaNode 配置
│   └── data_node.conf          # DataNode 配置
├── lib/                        # 运行时动态库
├── data/
│   ├── meta/                   # MetaNode 数据目录
│   │   ├── raft/               # Raft 日志 & 快照
│   │   └── rocksdb/            # 元数据 KV 存储
│   └── data/                   # DataNode 数据目录
│       ├── segment_000001.dat  # Segment 文件（256MB）
│       ├── segment_000002.dat
│       └── ...
├── logs/                       # 日志目录
│   ├── meta_node.log
│   └── data_node.log
└── run/                        # PID 文件
    ├── meta_node.pid
    └── data_node.pid
```

---

## 四、配置说明

### 4.1 MetaNode 配置 (`etc/meta_node.conf`)

```ini
# ===== MetaNode 配置 =====

# 节点 ID（集群中唯一，单节点模式设为 1）
meta.node_id=1

# 监听地址（Client + DataNode 连接用）
meta.listen_addr=0.0.0.0:50050

# 数据目录
meta.data_dir=/opt/openfs/data/meta

# Raft 集群节点列表（单节点模式写自身地址即可）
meta.raft_peers=meta1:50050

# ===== 日志 =====
log_level=INFO
# log_dir=/opt/openfs/logs
```

### 4.2 DataNode 配置 (`etc/data_node.conf`)

```ini
# ===== DataNode 配置 =====

# 监听地址
data.listen_addr=0.0.0.0:50051

# 数据目录（建议挂载独立数据盘）
data.data_dir=/opt/openfs/data/data

# MetaNode 地址
data.meta_addr=meta1:50050

# Segment 大小（字节），默认 1GB
data.segment_size=1073741824

# 最大 Segment 数量
data.max_segments=1024

# ===== 日志 =====
log_level=INFO
```

### 4.3 多节点集群配置示例

**3 节点 MetaNode + 3 节点 DataNode 部署：**

| 主机名 | 角色 | IP | MetaNode 端口 | DataNode 端口 |
|--------|------|----|---------------|---------------|
| meta1 | MetaNode (Leader) | 10.0.0.1 | 50050 | - |
| meta2 | MetaNode (Follower) | 10.0.0.2 | 50050 | - |
| meta3 | MetaNode (Follower) | 10.0.0.3 | 50050 | - |
| data1 | DataNode | 10.0.1.1 | - | 50051 |
| data2 | DataNode | 10.0.1.2 | - | 50051 |
| data3 | DataNode | 10.0.1.3 | - | 50051 |

meta1 的 `meta_node.conf`：
```ini
meta.node_id=1
meta.listen_addr=0.0.0.0:50050
meta.data_dir=/opt/openfs/data/meta
meta.raft_peers=meta1:50050,meta2:50050,meta3:50050
```

data1 的 `data_node.conf`：
```ini
data.listen_addr=0.0.0.0:50051
data.data_dir=/data/openfs
data.meta_addr=meta1:50050
data.segment_size=1073741824
```

---

## 五、系统调优

### 5.1 RHEL / CentOS / Rocky

```bash
# ---- 文件描述符限制 ----
cat >> /etc/security/limits.conf << 'EOF'
* soft nofile 1048576
* hard nofile 1048576
* soft nproc 1048576
* hard nproc 1048576
EOF

# ---- 内核参数 ----
cat >> /etc/sysctl.conf << 'EOF'
# 网络优化
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_fin_timeout = 15
net.ipv4.tcp_tw_reuse = 1
net.ipv4.ip_local_port_range = 1024 65535

# 文件系统
fs.file-max = 1048576
fs.inotify.max_user_watches = 1048576

# 内存
vm.swappiness = 1
vm.dirty_ratio = 10
vm.dirty_background_ratio = 5
vm.overcommit_memory = 1

# DataNode 大页内存（可选，对大内存机器有益）
# vm.nr_hugepages = 1024
EOF
sysctl -p

# ---- SELinux（生产建议关闭或设为 permissive）----
sudo setenforce 0
sudo sed -i 's/SELINUX=enforcing/SELINUX=permissive/' /etc/selinux/config

# ---- 防火墙 ----
sudo firewall-cmd --permanent --add-port=50050/tcp  # MetaNode
sudo firewall-cmd --permanent --add-port=50051/tcp  # DataNode
sudo firewall-cmd --reload

# ---- 磁盘调度器（NVMe 盘使用 none，HDD 使用 mq-deadline）----
echo 'none' | sudo tee /sys/block/nvme0n1/queue/scheduler
echo 'mq-deadline' | sudo tee /sys/block/sda/queue/scheduler

# ---- 透明大页关闭（数据库/存储类服务建议关闭）----
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/defrag
```

### 5.2 Ubuntu / Debian

```bash
# ---- 文件描述符限制 ----
cat >> /etc/security/limits.conf << 'EOF'
* soft nofile 1048576
* hard nofile 1048576
* soft nproc 1048576
* hard nproc 1048576
EOF

# ---- 内核参数（与 RHEL 相同）----
cat >> /etc/sysctl.conf << 'EOF'
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_fin_timeout = 15
net.ipv4.tcp_tw_reuse = 1
net.ipv4.ip_local_port_range = 1024 65535
fs.file-max = 1048576
vm.swappiness = 1
vm.dirty_ratio = 10
vm.dirty_background_ratio = 5
vm.overcommit_memory = 1
EOF
sysctl -p

# ---- UFW 防火墙 ----
sudo ufw allow 50050/tcp
sudo ufw allow 50051/tcp

# ---- 磁盘调度器 ----
echo 'none' | sudo tee /sys/block/nvme0n1/queue/scheduler

# ---- 透明大页 ----
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
```

---

## 六、Systemd 服务管理

### 6.1 MetaNode 服务

创建 `/etc/systemd/system/openfs-meta.service`：

```ini
[Unit]
Description=OpenFS MetaNode
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=openfs
Group=openfs
ExecStart=/opt/openfs/bin/meta_node /opt/openfs/etc/meta_node.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10
LimitNOFILE=1048576
LimitNPROC=1048576

# 日志输出到 journald
StandardOutput=journal
StandardError=journal
SyslogIdentifier=openfs-meta

# 安全加固
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/opt/openfs/data/meta /opt/openfs/logs /opt/openfs/run

[Install]
WantedBy=multi-user.target
```

### 6.2 DataNode 服务

创建 `/etc/systemd/system/openfs-data.service`：

```ini
[Unit]
Description=OpenFS DataNode
After=network-online.target openfs-meta.service
Wants=network-online.target

[Service]
Type=simple
User=openfs
Group=openfs
ExecStart=/opt/openfs/bin/data_node /opt/openfs/etc/data_node.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=10
LimitNOFILE=1048576
LimitNPROC=1048576

StandardOutput=journal
StandardError=journal
SyslogIdentifier=openfs-data

# 安全加固
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/data/openfs /opt/openfs/logs /opt/openfs/run

[Install]
WantedBy=multi-user.target
```

### 6.3 服务操作

```bash
# ---- 创建运行用户 ----
sudo useradd -r -s /sbin/nologin -d /opt/openfs openfs
sudo chown -R openfs:openfs /opt/openfs

# ---- 启动服务 ----
sudo systemctl daemon-reload
sudo systemctl enable openfs-meta openfs-data
sudo systemctl start openfs-meta
# 等 MetaNode 启动完成后再启动 DataNode
sleep 3
sudo systemctl start openfs-data

# ---- 查看状态 ----
sudo systemctl status openfs-meta
sudo systemctl status openfs-data

# ---- 查看日志 ----
journalctl -u openfs-meta -f
journalctl -u openfs-data -f

# ---- 停止服务 ----
sudo systemctl stop openfs-data
sudo systemctl stop openfs-meta

# ---- 重启 ----
sudo systemctl restart openfs-meta
sleep 3
sudo systemctl restart openfs-data
```

---

## 七、功能验证

### 7.1 基础连通性检查

```bash
# 检查 MetaNode 端口监听
ss -tlnp | grep 50050

# 检查 DataNode 端口监听
ss -tlnp | grep 50051

# 检查 DataNode 是否注册到 MetaNode（查看日志）
journalctl -u openfs-data --no-pager | grep "Registered with MetaNode"
```

### 7.2 gRPC 接口验证

安装 grpcurl：
```bash
# RHEL
sudo dnf install -y grpcurl 2>/dev/null || \
  sudo curl -L -o /usr/local/bin/grpcurl \
    https://github.com/fullstorydev/grpcurl/releases/download/v1.8.9/grpcurl_1.8.9_linux_x86_64.tar.gz && \
  sudo tar xzf /usr/local/bin/grpcurl -C /usr/local/bin/

# Ubuntu
sudo snap install grpcurl 2>/dev/null || \
  wget -qO- https://github.com/fullstorydev/grpcurl/releases/download/v1.8.9/grpcurl_1.8.9_linux_x86_64.tar.gz | \
  sudo tar xz -C /usr/local/bin/
```

```bash
# 列出 MetaNode 服务
grpcurl -plaintext localhost:50050 list

# 列出 DataNode 服务
grpcurl -plaintext localhost:50051 list

# ---- 元数据操作验证 ----

# 创建文件
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/hello.txt","mode":420,"uid":1000,"gid":1000,"file_size":1024}' \
  localhost:50050 openfs.MetaService/CreateFsFile

# 查询文件
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/hello.txt"}' \
  localhost:50050 openfs.MetaService/GetFileInfo

# 创建目录
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/testdir","mode":493,"uid":1000,"gid":1000}' \
  localhost:50050 openfs.MetaService/MkDir

# 读取目录
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/"}' \
  localhost:50050 openfs.MetaService/ReadDir

# 重命名
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"src_path":"/hello.txt","dst_path":"/hello_renamed.txt"}' \
  localhost:50050 openfs.MetaService/Rename

# 删除文件
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/hello_renamed.txt"}' \
  localhost:50050 openfs.MetaService/RemoveFsFile

# ---- 数据读写验证 ----

# 写入数据块
grpcurl -plaintext -import-path proto -proto data_service.proto \
  -d '{"block_id":1,"crc32":907060870,"data":"aGVsbG8gb3BlbmZz"}' \
  localhost:50051 openfs.DataService/WriteBlock

# 读取数据块（segment_id 和 offset 从 WriteBlock 返回值获取）
grpcurl -plaintext -import-path proto -proto data_service.proto \
  -d '{"block_id":1,"segment_id":1,"offset":4096}' \
  localhost:50051 openfs.DataService/ReadBlock
```

### 7.3 端到端流程验证

```bash
# 完整写入-读取流程（需要 MetaNode + DataNode 联动）

# 1. 注册 DataNode
grpcurl -plaintext -import-path proto -proto node_service.proto \
  -d '{"address":"localhost:50051","capacity":1073741824}' \
  localhost:50050 openfs.NodeService/Register

# 2. 创建文件
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"path":"/e2e_test.dat","mode":420,"uid":1000,"gid":1000,"file_size":4194304}' \
  localhost:50050 openfs.MetaService/CreateFsFile

# 3. 分配 Block（返回的 inode_id 填入下述请求）
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"inode_id":<INODE_ID>,"block_count":1,"level":"BLOCK_LEVEL_L2"}' \
  localhost:50050 openfs.MetaService/AllocateBlocks

# 4. 写入 Block（使用 AllocateBlocks 返回的 block_id）
grpcurl -plaintext -import-path proto -proto data_service.proto \
  -d '{"block_id":<BLOCK_ID>,"crc32":<CRC>,"data":"<BASE64_DATA>"}' \
  localhost:50051 openfs.DataService/WriteBlock

# 5. 提交 Block
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"inode_id":<INODE_ID>,"blocks":[...]}' \
  localhost:50050 openfs.MetaService/CommitBlocks

# 6. 查询 Block 位置
grpcurl -plaintext -import-path proto -proto meta_service.proto \
  -d '{"inode_id":<INODE_ID>}' \
  localhost:50050 openfs.MetaService/GetBlockLocations

# 7. 读取 Block（使用 GetBlockLocations 返回的 segment_id/offset）
grpcurl -plaintext -import-path proto -proto data_service.proto \
  -d '{"block_id":<BLOCK_ID>,"segment_id":<SEG_ID>,"offset":<OFFSET>}' \
  localhost:50051 openfs.DataService/ReadBlock
```

---

## 八、运维操作

### 8.1 扩容 DataNode

```bash
# 1. 在新节点上部署二进制和配置
# 2. 修改 data_node.conf 中的 meta_addr 指向 MetaNode
# 3. 启动 DataNode，自动注册到 MetaNode
sudo systemctl start openfs-data

# 4. 验证注册成功
journalctl -u openfs-data | grep "Registered with MetaNode"
```

### 8.2 滚动升级

```bash
# MetaNode 升级（单节点模式）
sudo systemctl stop openfs-meta
sudo cp /opt/openfs/bin/meta_node /opt/openfs/bin/meta_node.bak
sudo cp <new_meta_node> /opt/openfs/bin/meta_node
sudo systemctl start openfs-meta

# DataNode 逐节点升级
for host in data1 data2 data3; do
    ssh $host "sudo systemctl stop openfs-data"
    scp <new_data_node> $host:/opt/openfs/bin/data_node
    ssh $host "sudo systemctl start openfs-data"
    # 等待节点恢复并注册
    sleep 10
    ssh $host "journalctl -u openfs-data --since '1 min ago' | grep 'Registered'"
done
```

### 8.3 日志管理

```bash
# journald 持久化配置（RHEL/Ubuntu 通用）
sudo mkdir -p /var/log/journal
sudo systemd-tmpfiles --create --prefix /var/log/journal

# 日志轮转：/etc/systemd/journald.conf
[Journal]
SystemMaxUse=10G
SystemMaxFileSize=100M
MaxRetentionSec=30day
```

### 8.4 数据目录规划建议

```bash
# DataNode 数据盘建议使用独立挂载
# /etc/fstab 示例
/dev/nvme0n1p1  /data/openfs  xfs  noatime,nodiratime,discard  0 2

# XFS 创建时指定 inode 大小和分配组数
sudo mkfs.xfs -f -i size=512 -d agcount=32 /dev/nvme0n1p1
```

---

## 九、监控告警（预留）

> 当前版本尚无内置 Prometheus 指标导出，以下为规划中的监控项。

| 监控项 | 指标 | 告警阈值 |
|--------|------|----------|
| MetaNode 进程存活 | process_up | = 0 持续 30s |
| DataNode 进程存活 | process_up | = 0 持续 30s |
| DataNode 心跳延迟 | heartbeat_lag_ms | > 5000ms |
| DataNode 磁盘使用率 | disk_usage_ratio | > 85% |
| MetaNode RPC 延迟 P99 | rpc_latency_p99_ms | > 100ms |
| DataNode 写入吞吐 | write_throughput_mb | < 预期 50% |
| Segment 文件数量 | segment_count | 接近 max_segments |
| Block CRC 校验失败 | crc_mismatch_count | > 0 |

---

## 十、故障排查

### 10.1 常见问题

| 现象 | 可能原因 | 排查命令 |
|------|----------|----------|
| DataNode 无法注册到 MetaNode | 网络不通/端口不通 | `telnet meta1 50050` |
| 写入 Block 返回 kNoSpace | 数据盘满 | `df -h /data/openfs` |
| MetaNode 启动失败 | 数据目录权限 | `ls -la /opt/openfs/data/meta` |
| DataNode 心跳丢失 | 进程卡死/OOM | `dmesg \| grep -i oom` |
| gRPC 连接超时 | 防火墙/SELinux | `sudo tcpdump -i any port 50050` |

### 10.2 诊断脚本

```bash
#!/bin/bash
# openfs-diag.sh - 快速诊断脚本

echo "=== OpenFS 诊断报告 ==="
echo "时间: $(date)"
echo

echo "--- 进程状态 ---"
systemctl is-active openfs-meta 2>/dev/null || echo "MetaNode: 未运行"
systemctl is-active openfs-data 2>/dev/null || echo "DataNode: 未运行"
echo

echo "--- 端口监听 ---"
ss -tlnp | grep -E '50050|50051'
echo

echo "--- 磁盘空间 ---"
df -h /opt/openfs/data /data/openfs 2>/dev/null
echo

echo "--- 内存 ---"
free -h
echo

echo "--- 最近日志 (MetaNode) ---"
journalctl -u openfs-meta --no-pager -n 20 2>/dev/null
echo

echo "--- 最近日志 (DataNode) ---"
journalctl -u openfs-data --no-pager -n 20 2>/dev/null
echo

echo "--- 网络连通性 ---"
timeout 3 bash -c 'echo > /dev/tcp/localhost/50050' 2>/dev/null && echo "MetaNode 端口: OK" || echo "MetaNode 端口: FAIL"
timeout 3 bash -c 'echo > /dev/tcp/localhost/50051' 2>/dev/null && echo "DataNode 端口: OK" || echo "DataNode 端口: FAIL"
```