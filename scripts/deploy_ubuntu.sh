#!/bin/bash
# ============================================================
# OpenFS Ubuntu 22.04 一键部署脚本
# 用法: sudo bash deploy_ubuntu.sh [--single-node | --cluster]
# ============================================================
set -euo pipefail

OPENFS_HOME="/opt/openfs"
OPENFS_BIN="/usr/local/bin"
OPENFS_ETC="/etc/openfs"
OPENFS_VAR="/var/lib/openfs"
OPENFS_LOG="/var/log/openfs"
OPENFS_RUN="/var/run/openfs"
OPENFS_USER="openfs"

DEPLOY_MODE="${1:---single-node}"

echo "============================================"
echo "  OpenFS 部署脚本 for Ubuntu 22.04"
echo "  模式: $DEPLOY_MODE"
echo "============================================"

# ---- 1. 系统依赖安装 ----
echo ""
echo "[1/7] 安装系统依赖..."
apt update
apt install -y \
    build-essential cmake ninja-build pkg-config \
    libprotobuf-dev protobuf-compiler \
    libgrpc++-dev \
    libspdlog-dev \
    libgtest-dev libgflags-dev \
    uuid-dev \
    python3 python3-pip \
    fio \
    liburing-dev 2>/dev/null || true

# 安装 ghz (gRPC 压测工具)
if ! command -v ghz &>/dev/null; then
    echo "  安装 ghz..."
    GHZ_VER=0.120.0
    wget -q "https://github.com/bojand/ghz/releases/download/v${GHZ_VER}/ghz_${GHZ_VER}_linux_x86_64.tar.gz" \
        -O /tmp/ghz.tar.gz || { echo "ghz 下载失败, 跳过"; }
    if [ -f /tmp/ghz.tar.gz ]; then
        tar xzf /tmp/ghz.tar.gz -C /usr/local/bin ghz
        rm /tmp/ghz.tar.gz
    fi
fi

# 安装 Python 依赖
pip3 install grpcio grpcio-tools 2>/dev/null || true

# ---- 2. 编译 OpenFS ----
echo ""
echo "[2/7] 编译 OpenFS..."
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ ! -f "$PROJECT_DIR/CMakeLists.txt" ]; then
    PROJECT_DIR="$(pwd)"
fi

cd "$PROJECT_DIR"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
cmake --build build --config Release -j"$(nproc)" 2>&1 | tail -5

# ---- 3. 安装到系统 ----
echo ""
echo "[3/7] 安装到系统..."
cmake --install build --prefix /usr/local 2>/dev/null || {
    cp -v build/meta_node build/data_node build/openfs_cli "$OPENFS_BIN/"
}

mkdir -p "$OPENFS_ETC" "$OPENFS_VAR/meta" "$OPENFS_VAR/data" "$OPENFS_LOG" "$OPENFS_RUN"

# ---- 4. 创建 openfs 用户 ----
echo ""
echo "[4/7] 创建 openfs 用户..."
if ! id "$OPENFS_USER" &>/dev/null; then
    useradd -r -s /sbin/nologin -d "$OPENFS_VAR" "$OPENFS_USER"
fi
usermod -aG disk "$OPENFS_USER" 2>/dev/null || true
chown -R "$OPENFS_USER:$OPENFS_USER" "$OPENFS_VAR" "$OPENFS_LOG" "$OPENFS_RUN"

# ---- 5. 生成配置文件 ----
echo ""
echo "[5/7] 生成配置文件..."

if [ "$DEPLOY_MODE" = "--single-node" ]; then
    cat > "$OPENFS_ETC/meta_node.conf" << 'METACONF'
meta.node_id=1
meta.listen_addr=0.0.0.0:8100
meta.data_dir=/var/lib/openfs/meta
meta.raft_peers=
log_level=INFO
METACONF

    cat > "$OPENFS_ETC/data_node.conf" << 'DATACONF'
data.listen_addr=0.0.0.0:8200
data.meta_addr=127.0.0.1:8100
data.data_dir=/var/lib/openfs/data
data.disk_paths=/var/lib/openfs/data/disk0.ofs
data.disk_size=1073741824
data.wal_blocks=256
log_level=INFO
DATACONF

    echo "  单节点配置已生成 (文件模拟磁盘 1GB)"

elif [ "$DEPLOY_MODE" = "--cluster" ]; then
    LOCAL_IP=$(hostname -I | awk '{print $1}')

    cat > "$OPENFS_ETC/meta_node.conf" << METACONF
meta.node_id=1
meta.listen_addr=0.0.0.0:8100
meta.data_dir=/var/lib/openfs/meta
meta.raft_peers=${LOCAL_IP}:8100
log_level=INFO
METACONF

    cat > "$OPENFS_ETC/data_node.conf" << DATACONF
data.listen_addr=0.0.0.0:8200
data.meta_addr=${LOCAL_IP}:8100
data.disk_paths=/var/lib/openfs/data/disk0.ofs
data.disk_size=0
data.wal_blocks=1024
log_level=INFO
DATACONF

    echo "  集群配置已生成 (本地IP: $LOCAL_IP)"
fi

# ---- 6. 安装 systemd 服务 ----
echo ""
echo "[6/7] 安装 systemd 服务..."

cat > /etc/systemd/system/openfs-meta.service << 'EOF'
[Unit]
Description=OpenFS MetaNode Service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=openfs
Group=openfs
ExecStart=/usr/local/bin/meta_node /etc/openfs/meta_node.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5
LimitNOFILE=655360

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/openfs-data.service << 'EOF'
[Unit]
Description=OpenFS DataNode Service
After=network-online.target openfs-meta.service
Wants=network-online.target

[Service]
Type=simple
User=openfs
Group=openfs
SupplementaryGroups=disk
ExecStart=/usr/local/bin/data_node /etc/openfs/data_node.conf
ExecReload=/bin/kill -HUP $MAINPID
Restart=on-failure
RestartSec=5
LimitNOFILE=655360

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload

# ---- 7. 内核参数调优 ----
echo ""
echo "[7/7] 内核参数调优..."

cat > /etc/security/limits.d/99-openfs.conf << 'EOF'
openfs soft nofile 655360
openfs hard nofile 655360
root soft nofile 655360
root hard nofile 655360
EOF

cat > /etc/sysctl.d/99-openfs.conf << 'EOF'
vm.swappiness=1
kernel.io_uring_disabled=0
net.core.somaxconn=65535
net.ipv4.tcp_max_syn_backlog=65535
net.ipv4.tcp_tw_reuse=1
EOF
sysctl --system 2>/dev/null || true

for dev in /sys/block/nvme*; do
    if [ -d "$dev" ]; then
        echo "none" > "$dev/queue/scheduler" 2>/dev/null || true
    fi
done

echo ""
echo "============================================"
echo "  OpenFS 部署完成!"
echo "============================================"
echo ""
echo "启动服务:"
echo "  sudo systemctl start openfs-meta"
echo "  sudo systemctl start openfs-data"
echo ""
echo "查看状态:"
echo "  sudo systemctl status openfs-meta"
echo "  sudo systemctl status openfs-data"
echo ""
echo "查看日志:"
echo "  sudo journalctl -u openfs-meta -f"
echo "  sudo journalctl -u openfs-data -f"
echo ""
echo "配置文件:"
echo "  $OPENFS_ETC/meta_node.conf"
echo "  $OPENFS_ETC/data_node.conf"