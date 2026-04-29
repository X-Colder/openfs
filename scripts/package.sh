#!/bin/bash
# ============================================================
# OpenFS 部署物料打包脚本
#
# 两种模式:
#   1. 本地编译打包:  bash scripts/package.sh --build
#      (二进制只能在当前平台运行)
#
#   2. 源码打包:      bash scripts/package.sh --source
#      (在目标服务器上编译, 确保二进制兼容)
#      ★ 推荐: 用于跨平台部署 (如在 macOS 开发, 部署到 Linux)
#
# 用法:
#   bash scripts/package.sh --source              # 源码打包 (推荐)
#   bash scripts/package.sh --build               # 本地编译+打包
#   bash scripts/package.sh --build --target linux # 同上
# ============================================================
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.0.0"
PKG_MODE="source"  # source 或 binary

# 解析参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --source)
            PKG_MODE="source"
            shift
            ;;
        --build)
            PKG_MODE="binary"
            shift
            ;;
        -h|--help)
            echo "用法: bash scripts/package.sh [--source | --build]"
            echo ""
            echo "  --source  源码打包 (默认, 推荐)"
            echo "            打包源码+配置+部署脚本, 在目标服务器上编译后部署"
            echo "            适用于: macOS 开发 → Linux 部署"
            echo ""
            echo "  --build   本地编译+打包"
            echo "            编译当前平台的二进制并打包"
            echo "            适用于: 目标服务器与当前机器同平台"
            exit 0
            ;;
        *)
            echo "未知参数: $1, 用法: bash scripts/package.sh [--source | --build]"
            exit 1
            ;;
    esac
done

# 检测平台信息
HOST_OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
HOST_ARCH="$(uname -m)"
if [ "$HOST_ARCH" = "x86_64" ]; then
    HOST_ARCH="amd64"
elif [ "$HOST_ARCH" = "aarch64" ] || [ "$HOST_ARCH" = "arm64" ]; then
    HOST_ARCH="arm64"
fi

if [ "$PKG_MODE" = "source" ]; then
    PKG_NAME="openfs-deploy-${VERSION}-source"
else
    PKG_NAME="openfs-deploy-${VERSION}-${HOST_OS}-${HOST_ARCH}"
fi

PKG_DIR="/tmp/${PKG_NAME}"

echo "============================================"
echo "  OpenFS 部署物料打包"
echo "  版本:  $VERSION"
echo "  模式:  $PKG_MODE"
if [ "$PKG_MODE" = "binary" ]; then
echo "  平台:  $HOST_OS/$HOST_ARCH"
fi
echo "  产出:  ${PKG_NAME}.tar.gz"
echo "============================================"

# ============================================================
# 模式一: 源码打包 (推荐, 跨平台部署)
# ============================================================
if [ "$PKG_MODE" = "source" ]; then
    echo ""
    echo "[1/3] 准备源码打包目录..."
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR"/{src,configs,scripts,proto,cmake,docs}

    # 源码
    echo "  复制源码..."
    cp -r "$PROJECT_DIR/src"/*      "$PKG_DIR/src/"
    cp -r "$PROJECT_DIR/proto"/*    "$PKG_DIR/proto/"
    cp -r "$PROJECT_DIR/cmake"/*    "$PKG_DIR/cmake/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/CMakeLists.txt" "$PKG_DIR/"

    # 配置文件
    echo "  复制配置文件..."
    cp -v "$PROJECT_DIR/configs/meta_node.conf"       "$PKG_DIR/configs/meta_node.conf.example"
    cp -v "$PROJECT_DIR/configs/data_node.conf"       "$PKG_DIR/configs/data_node.conf.example"
    cp -v "$PROJECT_DIR/configs/openfs-meta.service"  "$PKG_DIR/configs/"
    cp -v "$PROJECT_DIR/configs/openfs-data.service"  "$PKG_DIR/configs/"

    # 部署脚本
    echo "  复制部署脚本..."
    cp -v "$PROJECT_DIR/scripts/deploy_ubuntu.sh"     "$PKG_DIR/scripts/"
    cp -v "$PROJECT_DIR/scripts/run_benchmark.sh"     "$PKG_DIR/scripts/"
    cp -v "$PROJECT_DIR/scripts/uninstall.sh"         "$PKG_DIR/scripts/"

    # 文档
    echo "  复制文档..."
    cp -v "$PROJECT_DIR/README.md"           "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/architecture.md" "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/deployment.md"   "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/benchmark.md"    "$PKG_DIR/docs/" 2>/dev/null || true

    # 测试 (可选)
    if [ -d "$PROJECT_DIR/tests" ]; then
        mkdir -p "$PKG_DIR/tests"
        cp -r "$PROJECT_DIR/tests"/* "$PKG_DIR/tests/" 2>/dev/null || true
    fi

    # ---- 生成一键部署脚本 ----
    echo ""
    echo "[2/3] 生成一键部署脚本..."

    cat > "$PKG_DIR/install.sh" << 'INSTALL_EOF'
#!/bin/bash
# ============================================================
# OpenFS 一键部署脚本 (源码模式)
# 在目标服务器上编译并安装 OpenFS
#
# 用法: sudo bash install.sh [选项]
#
# 选项:
#   --single-node  单节点部署 (默认)
#   --cluster      集群模式部署
#   --verbose      详细输出, 编译时实时显示 (排查问题用)
#
# 环境变量:
#   OPENFS_INSTALL_DEPS=no   跳过系统依赖安装
#   OPENFS_BUILD_TYPE=Debug  使用 Debug 模式编译
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="--single-node"
VERBOSE=false

# 解析参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --single-node) MODE="--single-node"; shift ;;
        --cluster)     MODE="--cluster"; shift ;;
        --verbose|-v)  VERBOSE=true; shift ;;
        -h|--help)
            echo "用法: sudo bash install.sh [--single-node | --cluster] [--verbose]"
            echo ""
            echo "  --single-node  单节点部署 (默认)"
            echo "  --cluster      集群模式"
            echo "  --verbose      详细输出, 实时显示编译日志"
            echo ""
            echo "  环境变量:"
            echo "    OPENFS_INSTALL_DEPS=no   跳过依赖安装"
            echo "    OPENFS_BUILD_TYPE=Debug  Debug 编译"
            exit 0
            ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

INSTALL_DEPS="${OPENFS_INSTALL_DEPS:-yes}"
BUILD_TYPE="${OPENFS_BUILD_TYPE:-Release}"
NPROC="$(nproc 2>/dev/null || echo 4)"

OPENFS_BIN="/usr/local/bin"
OPENFS_ETC="/etc/openfs"
OPENFS_VAR="/var/lib/openfs"
OPENFS_LOG="/var/log/openfs"
OPENFS_USER="openfs"

echo "============================================"
echo "  OpenFS 一键部署 (源码编译模式)"
echo "  模式: $MODE"
echo "  源码: $SCRIPT_DIR"
echo "============================================"

# ---- 0. 安装编译依赖 ----
echo ""
echo "[0/7] 安装编译依赖..."

if [ "$INSTALL_DEPS" = "yes" ]; then
    if command -v apt &>/dev/null; then
        echo "  检测到 Ubuntu/Debian, 安装依赖..."
        apt update -qq
        apt install -y -qq \
            build-essential cmake ninja-build pkg-config \
            libprotobuf-dev protobuf-compiler \
            libgrpc++-dev libspdlog-dev \
            libgtest-dev libgflags-dev \
            uuid-dev liburing-dev 2>/dev/null || {
            echo "  警告: 部分依赖安装失败, 尝试继续编译..."
        }
    elif command -v dnf &>/dev/null; then
        echo "  检测到 RHEL/Rocky, 安装依赖..."
        dnf install -y \
            gcc-c++ cmake ninja-build pkgconfig \
            protobuf-devel protobuf-compiler \
            grpc-devel spdlog-devel \
            gtest-devel gflags-devel \
            libuuid-devel liburing-devel 2>/dev/null || {
            echo "  警告: 部分依赖安装失败, 尝试继续编译..."
        }
    else
        echo "  未识别的包管理器, 请手动安装编译依赖"
        echo "  需要: cmake, protobuf, grpc, spdlog, gtest"
        echo "  继续..."
    fi
else
    echo "  跳过依赖安装 (OPENFS_INSTALL_DEPS=no)"
fi

# ---- 1. 编译 ----
echo ""
echo "[1/7] 编译 OpenFS ($BUILD_TYPE, ${NPROC} 线程)..."
cd "$SCRIPT_DIR"
mkdir -p build

# cmake 配置
echo "  运行 cmake..."
if $VERBOSE; then
    cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" 2>&1 | tee build/cmake_configure.log
    CMAKE_RC=${PIPESTATUS[0]}
else
    cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" > build/cmake_configure.log 2>&1
    CMAKE_RC=$?
fi
if [ "$CMAKE_RC" -ne 0 ]; then
    echo "  错误: cmake 配置失败! 详见 build/cmake_configure.log"
    echo ""
    echo "  ---- 最后 30 行错误 ----"
    tail -30 build/cmake_configure.log
    echo "  ------------------------"
    echo ""
    echo "  排查方法:"
    echo "    cat $SCRIPT_DIR/build/cmake_configure.log"
    echo "    常见原因: 缺少开发包 (libprotobuf-dev, libgrpc++-dev, libspdlog-dev 等)"
    exit 1
fi
echo "  cmake 配置成功"

# cmake 编译
echo "  运行编译 (日志: build/cmake_build.log)..."
if $VERBOSE; then
    cmake --build build --config "$BUILD_TYPE" -j"$NPROC" 2>&1 | tee build/cmake_build.log
    BUILD_RC=${PIPESTATUS[0]}
else
    cmake --build build --config "$BUILD_TYPE" -j"$NPROC" > build/cmake_build.log 2>&1
    BUILD_RC=$?
fi
if [ "$BUILD_RC" -ne 0 ]; then
    echo "  错误: 编译失败! 详见 build/cmake_build.log"
    echo ""
    echo "  ---- 编译错误 (最后 50 行) ----"
    tail -50 build/cmake_build.log
    echo "  --------------------------------"
    echo ""
    echo "  排查方法:"
    echo "    cat $SCRIPT_DIR/build/cmake_build.log"
    echo "    仅编译单个目标:  cmake --build build --target meta_node"
    echo "    单线程编译(避免错误刷屏): cmake --build build -j1"
    echo "    重新安装部署:      sudo bash install.sh --verbose"
    exit 1
fi
echo "  编译成功!"

# 可选: 运行单元测试
if [ -f "build/test_types" ]; then
    echo ""
    echo "  运行单元测试..."
    (cd build && ctest --output-on-failure -j"$NPROC" --timeout 30 2>&1 | tail -5) || {
        echo "  警告: 部分测试未通过 (不影响安装)"
    }
fi

# ---- 2. 安装二进制 ----
echo ""
echo "[2/7] 安装二进制..."
for bin in meta_node data_node openfs_cli; do
    if [ -f "build/$bin" ]; then
        cp -v "build/$bin" "$OPENFS_BIN/"
        chmod 755 "$OPENFS_BIN/$bin"
    fi
done

# ---- 3. 安装配置文件 ----
echo ""
echo "[3/7] 安装配置文件..."
mkdir -p "$OPENFS_ETC"

for conf in "$SCRIPT_DIR/configs/"*.example; do
    [ -f "$conf" ] || continue
    name=$(basename "$conf" .example)
    if [ ! -f "$OPENFS_ETC/$name" ]; then
        cp -v "$conf" "$OPENFS_ETC/$name"
    else
        echo "  保留已有 $name"
    fi
done

# 首次安装: 确保配置存在
[ ! -f "$OPENFS_ETC/meta_node.conf" ] && \
    cp "$SCRIPT_DIR/configs/meta_node.conf.example" "$OPENFS_ETC/meta_node.conf" 2>/dev/null || true
[ ! -f "$OPENFS_ETC/data_node.conf" ] && \
    cp "$SCRIPT_DIR/configs/data_node.conf.example" "$OPENFS_ETC/data_node.conf" 2>/dev/null || true

# ---- 4. 创建用户和目录 ----
echo ""
echo "[4/7] 创建 openfs 用户和目录..."
if ! id "$OPENFS_USER" &>/dev/null; then
    useradd -r -s /sbin/nologin -d "$OPENFS_VAR" "$OPENFS_USER"
fi
usermod -aG disk "$OPENFS_USER" 2>/dev/null || true
mkdir -p "$OPENFS_VAR"/{meta,data} "$OPENFS_LOG"
chown -R "$OPENFS_USER:$OPENFS_USER" "$OPENFS_VAR" "$OPENFS_LOG"

# ---- 5. 安装 systemd 服务 ----
echo ""
echo "[5/7] 安装 systemd 服务..."
if [ -d /etc/systemd/system ]; then
    for svc in "$SCRIPT_DIR/configs/"*.service; do
        [ -f "$svc" ] && cp -v "$svc" /etc/systemd/system/
    done
    systemctl daemon-reload
fi

# ---- 6. 内核参数调优 ----
echo ""
echo "[6/7] 内核参数调优..."
if [ -d /etc/security/limits.d ]; then
    cat > /etc/security/limits.d/99-openfs.conf << 'EOF'
openfs soft nofile 655360
openfs hard nofile 655360
root   soft nofile 655360
root   hard nofile 655360
EOF
fi
if [ -d /etc/sysctl.d ]; then
    cat > /etc/sysctl.d/99-openfs.conf << 'EOF'
vm.swappiness=1
net.core.somaxconn=65535
net.ipv4.tcp_max_syn_backlog=65535
net.ipv4.tcp_tw_reuse=1
EOF
    [ -f /proc/sys/kernel/io_uring_disabled ] && \
        echo "kernel.io_uring_disabled=0" >> /etc/sysctl.d/99-openfs.conf
    sysctl --system 2>/dev/null || true
fi
for dev in /sys/block/nvme*; do
    [ -d "$dev" ] && echo "none" > "$dev/queue/scheduler" 2>/dev/null || true
done

# ---- 7. 集群模式配置提示 ----
echo ""
echo "[7/7] 配置检查..."
if [ "$MODE" = "--cluster" ]; then
    LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}' || echo "YOUR_IP")
    echo "  集群模式! 请修改配置:"
    echo "    $OPENFS_ETC/meta_node.conf  → meta.raft_peers"
    echo "    $OPENFS_ETC/data_node.conf  → data.meta_addr, data.disk_paths"
fi

echo ""
echo "============================================"
echo "  OpenFS 部署完成!"
echo "============================================"
echo ""
echo "  启动: sudo systemctl start openfs-meta openfs-data"
echo "  状态: sudo systemctl status openfs-meta openfs-data"
echo "  日志: sudo journalctl -u openfs-data -f"
echo "  测试: bash $SCRIPT_DIR/scripts/run_benchmark.sh --quick"
echo "  CLI:  openfs_cli --meta localhost:8100 ls /"
INSTALL_EOF

    chmod +x "$PKG_DIR/install.sh"

fi  # end source mode

# ============================================================
# 模式二: 本地编译+打包 (同平台部署)
# ============================================================
if [ "$PKG_MODE" = "binary" ]; then
    echo ""
    echo "[1/4] 编译 Release 版本..."
    cd "$PROJECT_DIR"
    mkdir -p build
    NPROC_BUILD="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

    if ! cmake -B build -DCMAKE_BUILD_TYPE=Release > build/cmake_configure.log 2>&1; then
        echo "  错误: cmake 配置失败!"
        tail -30 build/cmake_configure.log
        exit 1
    fi
    if ! cmake --build build --config Release -j"$NPROC_BUILD" > build/cmake_build.log 2>&1; then
        echo "  错误: 编译失败!"
        tail -50 build/cmake_build.log
        exit 1
    fi

    echo ""
    echo "[2/4] 准备打包目录..."
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR"/{bin,etc,scripts,docs}

    echo "  复制二进制..."
    for bin in meta_node data_node openfs_cli; do
        [ -f "$PROJECT_DIR/build/$bin" ] && cp -v "$PROJECT_DIR/build/$bin" "$PKG_DIR/bin/"
    done

    echo "  复制配置文件..."
    cp -v "$PROJECT_DIR/configs/meta_node.conf"  "$PKG_DIR/etc/meta_node.conf.example"
    cp -v "$PROJECT_DIR/configs/data_node.conf"  "$PKG_DIR/etc/data_node.conf.example"
    cp -v "$PROJECT_DIR/configs/openfs-meta.service" "$PKG_DIR/etc/"
    cp -v "$PROJECT_DIR/configs/openfs-data.service" "$PKG_DIR/etc/"

    echo "  复制脚本和文档..."
    cp -v "$PROJECT_DIR/scripts/deploy_ubuntu.sh"  "$PKG_DIR/scripts/"
    cp -v "$PROJECT_DIR/scripts/run_benchmark.sh"  "$PKG_DIR/scripts/"
    cp -v "$PROJECT_DIR/scripts/uninstall.sh"      "$PKG_DIR/scripts/"
    cp -v "$PROJECT_DIR/README.md"          "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/architecture.md" "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/deployment.md"   "$PKG_DIR/docs/" 2>/dev/null || true
    cp -v "$PROJECT_DIR/docs/benchmark.md"    "$PKG_DIR/docs/" 2>/dev/null || true

    echo ""
    echo "[3/4] 生成部署脚本..."

    cat > "$PKG_DIR/install.sh" << 'BINARY_INSTALL_EOF'
#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:---single-node}"
OPENFS_BIN="/usr/local/bin"
OPENFS_ETC="/etc/openfs"
OPENFS_VAR="/var/lib/openfs"
OPENFS_LOG="/var/log/openfs"
OPENFS_USER="openfs"

echo "============================================"
echo "  OpenFS 一键部署 (二进制模式)"
echo "  模式: $MODE"
echo "============================================"

echo "[1/6] 安装二进制..."
for bin in "$SCRIPT_DIR/bin/"*; do
    [ -f "$bin" ] && [ -x "$bin" ] && cp -v "$bin" "$OPENFS_BIN/" && chmod 755 "$OPENFS_BIN/$(basename $bin)"
done

echo "[2/6] 安装配置..."
mkdir -p "$OPENFS_ETC"
for conf in "$SCRIPT_DIR/etc/"*.example; do
    [ -f "$conf" ] || continue
    name=$(basename "$conf" .example)
    [ ! -f "$OPENFS_ETC/$name" ] && cp -v "$conf" "$OPENFS_ETC/$name" || echo "  保留 $name"
done
[ ! -f "$OPENFS_ETC/meta_node.conf" ] && cp "$SCRIPT_DIR/etc/meta_node.conf.example" "$OPENFS_ETC/meta_node.conf" 2>/dev/null
[ ! -f "$OPENFS_ETC/data_node.conf" ] && cp "$SCRIPT_DIR/etc/data_node.conf.example" "$OPENFS_ETC/data_node.conf" 2>/dev/null

echo "[3/6] 创建用户..."
! id "$OPENFS_USER" &>/dev/null && useradd -r -s /sbin/nologin -d "$OPENFS_VAR" "$OPENFS_USER"
usermod -aG disk "$OPENFS_USER" 2>/dev/null || true
mkdir -p "$OPENFS_VAR"/{meta,data} "$OPENFS_LOG"
chown -R "$OPENFS_USER:$OPENFS_USER" "$OPENFS_VAR" "$OPENFS_LOG"

echo "[4/6] 安装 systemd 服务..."
[ -d /etc/systemd/system ] && cp "$SCRIPT_DIR/etc/"*.service /etc/systemd/system/ 2>/dev/null && systemctl daemon-reload

echo "[5/6] 内核调优..."
[ -d /etc/security/limits.d ] && cat > /etc/security/limits.d/99-openfs.conf << 'EOF'
openfs soft nofile 655360
openfs hard nofile 655360
root   soft nofile 655360
root   hard nofile 655360
EOF
[ -d /etc/sysctl.d ] && cat > /etc/sysctl.d/99-openfs.conf << 'EOF'
vm.swappiness=1
net.core.somaxconn=65535
net.ipv4.tcp_max_syn_backlog=65535
net.ipv4.tcp_tw_reuse=1
EOF
[ -f /proc/sys/kernel/io_uring_disabled ] && echo "kernel.io_uring_disabled=0" >> /etc/sysctl.d/99-openfs.conf
sysctl --system 2>/dev/null || true
for dev in /sys/block/nvme*; do [ -d "$dev" ] && echo "none" > "$dev/queue/scheduler" 2>/dev/null; done

echo "[6/6] 完成"
echo ""
echo "  启动: sudo systemctl start openfs-meta openfs-data"
echo "  日志: sudo journalctl -u openfs-data -f"
BINARY_INSTALL_EOF

    chmod +x "$PKG_DIR/install.sh"
fi  # end binary mode

# ============================================================
# 通用: 版本信息 + 打包
# ============================================================
cat > "$PKG_DIR/VERSION" << EOF
openfs ${VERSION}
mode ${PKG_MODE}
host ${HOST_OS}/${HOST_ARCH}
build_type Release
pack_time $(date -u '+%Y-%m-%d %H:%M:%S UTC')
git_commit $(cd "$PROJECT_DIR" && git rev-parse --short HEAD 2>/dev/null || echo "unknown")
EOF

(cd "$PKG_DIR" && find . -type f | sort | sed 's|^\./||' > MANIFEST)

echo ""
echo "[最终] 打包..."
cd /tmp
tar czf "${PKG_NAME}.tar.gz" "$PKG_NAME"
PKG_SIZE=$(du -h "${PKG_NAME}.tar.gz" | cut -f1)

cp "${PKG_NAME}.tar.gz" "$PROJECT_DIR/"
rm -rf "$PKG_DIR" "/tmp/${PKG_NAME}.tar.gz"

echo ""
echo "============================================"
echo "  打包完成!"
echo "============================================"
echo "  文件: ${PKG_NAME}.tar.gz ($PKG_SIZE)"
echo "  路径: $PROJECT_DIR/${PKG_NAME}.tar.gz"
echo ""
if [ "$PKG_MODE" = "source" ]; then
echo "  ★ 源码模式: 在目标服务器上编译, 确保二进制兼容"
echo ""
echo "  目标服务器操作:"
echo "    scp ${PKG_NAME}.tar.gz user@server:/tmp/"
echo "    ssh user@server"
echo "    cd /tmp && tar xzf ${PKG_NAME}.tar.gz"
echo "    cd ${PKG_NAME} && sudo bash install.sh"
echo ""
echo "  集群模式:"
echo "    sudo bash install.sh --cluster"
else
echo "  二进制模式: 仅适用于 $HOST_OS/$HOST_ARCH 平台"
echo ""
echo "  目标服务器操作:"
echo "    scp ${PKG_NAME}.tar.gz user@server:/tmp/"
echo "    ssh user@server"
echo "    cd /tmp && tar xzf ${PKG_NAME}.tar.gz"
echo "    cd ${PKG_NAME} && sudo bash install.sh"
fi