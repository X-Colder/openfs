# OpenFS 部署文档

## 一、系统要求

### 1.1 操作系统

| 系统 | 最低版本 | 推荐版本 |
|---|---|---|
| Ubuntu | 20.04 LTS | 22.04 LTS / 24.04 LTS |
| RHEL / CentOS | 8.x | 9.x |
| Rocky Linux | 8.x | 9.x |
| AlmaLinux | 8.x | 9.x |

### 1.2 硬件要求

| 组件 | 最低要求 | 推荐配置 |
|---|---|---|
| CPU | 4 核 | 8+ 核 |
| 内存 | 8 GB | 32+ GB |
| 磁盘（MetaNode） | 100 GB SSD | 500 GB NVMe SSD |
| 磁盘（DataNode） | 1 TB HDD | 4×4 TB NVMe + 8×12 TB HDD |
| 网络 | 1 GbE | 10/25 GbE |

### 1.3 软件依赖

| 依赖 | 最低版本 | 用途 |
|---|---|---|
| GCC | 9.x | C++17 编译器 |
| CMake | 3.16+ | 构建系统 |
| Ninja | 1.10+ | 构建加速 |
| pkg-config | 0.29+ | 依赖查找 |
| protobuf | 3.x | RPC 序列化 |
| gRPC | 1.50+ | RPC 框架 |
| spdlog | 1.x | 日志库 |
| RocksDB | 8.x | KV 存储（MetaNode） |
| liburing | 2.x | io_uring（可选，Linux 5.1+） |

---

## 二、Ubuntu 系列部署

### 2.1 安装编译工具链

```bash
# Ubuntu 22.04 / 24.04
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    libspdlog-dev \
    librocksdb-dev \
    libgtest-dev \
    libgflags-dev \
    uuid-dev

# 可选：安装 io_uring 支持（Linux 5.1+ 内核）
sudo apt install -y liburing-dev
```

### 2.2 使用 vcpkg 安装依赖（推荐）

如果系统包版本不满足要求，推荐使用 vcpkg：

```bash
# 安装 vcpkg
git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
/opt/vcpkg/bootstrap-vcpkg.sh

# 安装所需依赖
/opt/vcpkg/vcpkg install \
    protobuf \
    grpc \
    spdlog \
    rocksdb \
    gtest \
    gflags

# 设置环境变量
export CMAKE_PREFIX_PATH=/opt/vcpkg/installed/x64-linux
```

### 2.3 编译 OpenFS

```bash
# 获取源码
git clone https://github.com/X-Colder/openfs.git
cd openfs

# 配置构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/vcpkg/installed/x64-linux

# 编译
cmake --build build --config Release

# 运行测试
ctest --test-dir build --timeout 120
```

### 2.4 安装到系统

```bash
sudo cmake --install build --prefix /usr/local

# 安装配置文件
sudo mkdir -p /etc/openfs
sudo cp configs/meta_node.conf /etc/openfs/
sudo cp configs/data_node.conf /etc/openfs/

# 安装 systemd 服务
sudo cp configs/openfs-meta.service /etc/systemd/system/
sudo cp configs/openfs-data.service /etc/systemd/system/
sudo systemctl daemon-reload
```

---

## 三、RHEL / Rocky / Alma 系列部署

### 3.1 安装编译工具链

```bash
# RHEL 9 / Rocky 9 / AlmaLinux 9
sudo dnf install -y \
    gcc-c++ \
    cmake \
    ninja-build \
    pkgconfig \
    protobuf-devel \
    protobuf-compiler \
    grpc-devel \
    spdlog-devel \
    rocksdb-devel \
    gtest-devel \
    gflags-devel \
    libuuid-devel

# 可选：安装 io_uring 支持
sudo dnf install -y liburing-devel
```

### 3.2 RHEL 8 / CentOS 8 需要启用 PowerTools/CRB

```bash
# RHEL 8 / CentOS 8 需要先启用额外仓库
sudo dnf install -y epel-release
sudo dnf config-manager --set-enabled powertools  # CentOS 8
# 或
sudo subscription-manager repos --enable codeready-builder-for-rhel-8-x86_64-rpms  # RHEL 8

# Rocky 8 / AlmaLinux 8
sudo dnf config-manager --set-enabled crb

# 然后安装依赖
sudo dnf install -y \
    gcc-c++ cmake ninja-build pkgconfig \
    protobuf-devel protobuf-compiler \
    grpc-devel spdlog-devel rocksdb-devel \
    gtest-devel gflags-devel libuuid-devel
```

### 3.3 使用 vcpkg（推荐，解决版本问题）

RHEL 8 系统自带的 protobuf/grpc 版本可能较旧，推荐使用 vcpkg：

```bash
git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
/opt/vcpkg/bootstrap-vcpkg.sh

/opt/vcpkg/vcpkg install \
    protobuf grpc spdlog rocksdb gtest gflags

export CMAKE_PREFIX_PATH=/opt/vcpkg/installed/x64-linux
```

### 3.4 编译和安装

与 Ubuntu 相同，参见 2.3 和 2.4 节。

---

## 四、磁盘格式化与配置

### 4.1 磁盘格式化（将磁盘纳入 OpenFS 管理）

OpenFS 使用专有的磁盘格式直接管理物理磁盘，不依赖本地文件系统。

```bash
# 查看可用磁盘
lsblk

# 示例：将 /dev/nvme0n1 纳入 OpenFS
# 注意：这会清除磁盘上的所有数据！
# OpenFS 会在首次使用时自动格式化磁盘
```

### 4.2 单节点配置示例

**MetaNode 配置** (`/etc/openfs/meta_node.conf`)：

```ini
meta.node_id=1
meta.listen_addr=0.0.0.0:8100
meta.data_dir=/var/lib/openfs/meta
log_level=INFO
```

**DataNode 配置** (`/etc/openfs/data_node.conf`)：

```ini
# 使用文件模拟磁盘（测试/开发环境）
data.listen_addr=0.0.0.0:8200
data.meta_addr=127.0.0.1:8100
data.disk_paths=/var/lib/openfs/data/disk0.ofs
data.disk_size=1073741824
data.wal_blocks=256
log_level=INFO
```

### 4.3 生产环境多磁盘配置

```ini
# 使用裸设备（生产环境）
data.listen_addr=0.0.0.0:8200
data.meta_addr=10.0.1.10:8100
# 多块磁盘直接指定裸设备路径
data.disk_paths=/dev/nvme0n1,/dev/nvme1n1,/dev/sdb,/dev/sdc
data.wal_blocks=1024
log_level=INFO
```

### 4.4 磁盘权限配置

OpenFS 的 data_node 进程需要直接读写裸磁盘设备：

```bash
# 方法1：将 openfs 用户加入 disk 组
sudo usermod -aG disk openfs

# 方法2：使用 udev 规则设置权限
sudo tee /etc/udev/rules.d/99-openfs-disk.rules << 'EOF'
# 为 OpenFS 使用的 NVMe 设备设置权限
KERNEL=="nvme0n1", MODE="0660", GROUP="disk"
KERNEL=="nvme1n1", MODE="0660", GROUP="disk"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 五、服务管理

### 5.1 创建 openfs 用户

```bash
sudo useradd -r -s /sbin/nologin -d /var/lib/openfs openfs
sudo mkdir -p /var/lib/openfs/{meta,data}
sudo mkdir -p /var/log/openfs
sudo chown -R openfs:openfs /var/lib/openfs /var/log/openfs
```

### 5.2 启动服务

```bash
# 启动 MetaNode
sudo systemctl enable openfs-meta
sudo systemctl start openfs-meta
sudo systemctl status openfs-meta

# 启动 DataNode
sudo systemctl enable openfs-data
sudo systemctl start openfs-data
sudo systemctl status openfs-data
```

### 5.3 查看日志

```bash
# systemd 日志
sudo journalctl -u openfs-meta -f
sudo journalctl -u openfs-data -f

# 或配置 spdlog 输出到文件
# 在配置文件中设置 log_level=DEBUG 排查问题
```

### 5.4 停止和重启

```bash
sudo systemctl restart openfs-data
sudo systemctl stop openfs-data
```

---

## 六、多节点集群部署

### 6.1 集群拓扑示例

```
┌─────────────────────────────────────────────────────┐
│                   管理网络 10.0.1.0/24               │
│                                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ MetaNode1│  │ MetaNode2│  │ MetaNode3│          │
│  │ 10.0.1.10│  │ 10.0.1.11│  │ 10.0.1.12│          │
│  └──────────┘  └──────────┘  └──────────┘          │
│         Raft 共识 (端口 8100)                         │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                   数据网络 10.0.2.0/24               │
│                                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ DataNode1│  │ DataNode2│  │ DataNode3│          │
│  │ 10.0.2.20│  │ 10.0.2.21│  │ 10.0.2.22│          │
│  │ NVMe×4   │  │ NVMe×4   │  │ NVMe×4   │          │
│  └──────────┘  └──────────┘  └──────────┘          │
│         Block 读写 (端口 8200)                        │
└─────────────────────────────────────────────────────┘
```

### 6.2 MetaNode 集群配置

在每个 MetaNode 上修改 `/etc/openfs/meta_node.conf`：

**MetaNode1:**
```ini
meta.node_id=1
meta.listen_addr=0.0.0.0:8100
meta.data_dir=/var/lib/openfs/meta
meta.raft_peers=10.0.1.10:8100,10.0.1.11:8100,10.0.1.12:8100
log_level=INFO
```

**MetaNode2:**
```ini
meta.node_id=2
meta.listen_addr=0.0.0.0:8100
meta.data_dir=/var/lib/openfs/meta
meta.raft_peers=10.0.1.10:8100,10.0.1.11:8100,10.0.1.12:8100
log_level=INFO
```

### 6.3 DataNode 配置

在每个 DataNode 上修改 `/etc/openfs/data_node.conf`：

```ini
data.listen_addr=0.0.0.0:8200
data.meta_addr=10.0.1.10:8100
data.disk_paths=/dev/nvme0n1,/dev/nvme1n1,/dev/nvme2n1,/dev/nvme3n1
data.wal_blocks=1024
log_level=INFO
```

### 6.4 防火墙配置

```bash
# Ubuntu (ufw)
sudo ufw allow 8100/tcp  # MetaNode RPC
sudo ufw allow 8200/tcp  # DataNode RPC

# RHEL / Rocky (firewalld)
sudo firewall-cmd --permanent --add-port=8100/tcp
sudo firewall-cmd --permanent --add-port=8200/tcp
sudo firewall-cmd --reload
```

---

## 七、内核参数调优

### 7.1 io_uring 支持

```bash
# 检查内核版本（需要 5.1+）
uname -r

# 检查 io_uring 支持
cat /proc/sys/kernel/io_uring_disabled
# 0 = 启用, 2 = 禁用

# 启用 io_uring
echo 0 | sudo tee /proc/sys/kernel/io_uring_disabled
# 持久化
echo "kernel.io_uring_disabled=0" | sudo tee -a /etc/sysctl.d/99-openfs.conf
```

### 7.2 文件描述符限制

```bash
# 临时设置
ulimit -n 655360

# 持久化（systemd 已在 service 文件中设置 LimitNOFILE）
# 或在 /etc/security/limits.d/99-openfs.conf 中设置：
echo "openfs soft nofile 655360" | sudo tee /etc/security/limits.d/99-openfs.conf
echo "openfs hard nofile 655360" | sudo tee -a /etc/security/limits.d/99-openfs.conf
```

### 7.3 虚拟内存和 I/O 调度

```bash
# 减少 swap 使用
echo "vm.swappiness=1" | sudo tee -a /etc/sysctl.d/99-openfs.conf

# I/O 调度器（NVMe 建议 none/mq-deadline）
echo "none" | sudo tee /sys/block/nvme0n1/queue/scheduler

# 持久化 via udev 规则
sudo tee /etc/udev/rules.d/60-openfs-io-scheduler.rules << 'EOF'
ACTION=="add|change", KERNEL=="nvme[0-9]*", ATTR{queue/scheduler}="none"
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/scheduler}="mq-deadline"
EOF
```

---

## 八、验证部署

### 8.1 检查服务状态

```bash
# 检查进程
ps aux | grep -E "meta_node|data_node"

# 检查端口监听
ss -tlnp | grep -E "8100|8200"

# 检查磁盘格式化状态
# OpenFS 格式化的磁盘以 "OFSB0001" magic 开头
xxd -l 8 /dev/nvme0n1
# 应输出: "OFSB0001"
```

### 8.2 功能测试

```bash
# 编译后运行单元测试
cd build && ctest -V

# 端到端测试：通过 gRPC 客户端写入和读取 Block
# (需要先启动 MetaNode 和 DataNode)
```

---

## 九、故障排查

### 9.1 常见问题

| 问题 | 原因 | 解决方案 |
|---|---|---|
| 无法打开磁盘设备 | 权限不足 | `usermod -aG disk openfs` |
| O_DIRECT 打开失败 | 文件系统不支持 | 自动降级为缓冲 IO |
| io_uring 不可用 | 内核版本 < 5.1 | 升级内核或使用 fstream IO |
| gRPC 连接失败 | 防火墙/网络 | 检查端口和防火墙规则 |
| WAL 恢复失败 | 磁盘数据损坏 | 检查磁盘 SMART 状态 |

### 9.2 数据恢复

```bash
# OpenFS 启动时自动执行 WAL 恢复
# 查看恢复日志
sudo journalctl -u openfs-data | grep "Recovery"

# 如果需要手动触发恢复
# 停止服务 → 重启服务（自动恢复）
sudo systemctl restart openfs-data
```

---

## 十、卸载

```bash
# 停止服务
sudo systemctl stop openfs-meta openfs-data
sudo systemctl disable openfs-meta openfs-data

# 删除服务文件
sudo rm /etc/systemd/system/openfs-*.service
sudo systemctl daemon-reload

# 删除程序和配置
sudo rm /usr/local/bin/meta_node /usr/local/bin/data_node
sudo rm -rf /etc/openfs /var/lib/openfs /var/log/openfs

# 删除用户
sudo userdel openfs
```