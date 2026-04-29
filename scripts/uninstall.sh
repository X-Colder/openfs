#!/bin/bash
# ============================================================
# OpenFS 一键卸载脚本
#
# 用法: sudo bash uninstall.sh [--purge]
#
#   默认: 停止服务、卸载二进制、删除用户、清理 systemd
#   --purge: 额外删除配置文件、数据目录、日志 (不可恢复!)
#
# 也可从部署物料包内运行:
#   cd openfs-deploy-1.0.0-source && sudo bash scripts/uninstall.sh
# ============================================================
set -euo pipefail

OPENFS_BIN="/usr/local/bin"
OPENFS_ETC="/etc/openfs"
OPENFS_VAR="/var/lib/openfs"
OPENFS_LOG="/var/log/openfs"
OPENFS_USER="openfs"

PURGE=false

# 解析参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --purge)
            PURGE=true
            shift
            ;;
        -h|--help)
            echo "用法: sudo bash uninstall.sh [--purge]"
            echo ""
            echo "  默认    停止服务、卸载二进制、删除用户、清理 systemd"
            echo "          保留配置文件(/etc/openfs)和数据目录(/var/lib/openfs)"
            echo ""
            echo "  --purge 额外删除配置文件、数据目录、日志 (不可恢复!)"
            exit 0
            ;;
        *)
            echo "未知参数: $1"
            echo "用法: sudo bash uninstall.sh [--purge]"
            exit 1
            ;;
    esac
done

# 检查 root 权限
if [ "$(id -u)" -ne 0 ]; then
    echo "错误: 请使用 sudo 运行此脚本"
    exit 1
fi

echo "============================================"
echo "  OpenFS 卸载"
if $PURGE; then
echo "  模式: 完全清除 (--purge)"
echo "  警告: 将删除所有配置、数据和日志!"
else
echo "  模式: 标准卸载 (保留配置和数据)"
fi
echo "============================================"

# ---- 1. 停止服务 ----
echo ""
echo "[1/6] 停止服务..."
for svc in openfs-meta openfs-data; do
    if systemctl is-active "$svc" &>/dev/null; then
        systemctl stop "$svc"
        echo "  已停止 $svc"
    else
        echo "  $svc 未运行"
    fi
done

# ---- 2. 禁用并删除 systemd 服务 ----
echo ""
echo "[2/6] 清理 systemd 服务..."
for svc in openfs-meta openfs-data; do
    if systemctl is-enabled "$svc" &>/dev/null; then
        systemctl disable "$svc"
        echo "  已禁用 $svc"
    fi
    if [ -f "/etc/systemd/system/$svc.service" ]; then
        rm -f "/etc/systemd/system/$svc.service"
        echo "  已删除 $svc.service"
    fi
done
systemctl daemon-reload

# ---- 3. 删除二进制 ----
echo ""
echo "[3/6] 删除二进制文件..."
for bin in meta_node data_node openfs_cli; do
    if [ -f "$OPENFS_BIN/$bin" ]; then
        rm -f "$OPENFS_BIN/$bin"
        echo "  已删除 $OPENFS_BIN/$bin"
    else
        echo "  $OPENFS_BIN/$bin 不存在"
    fi
done

# ---- 4. 删除用户 ----
echo ""
echo "[4/6] 删除 openfs 用户..."
if id "$OPENFS_USER" &>/dev/null; then
    userdel "$OPENFS_USER"
    echo "  已删除用户 $OPENFS_USER"
else
    echo "  用户 $OPENFS_USER 不存在"
fi

# ---- 5. 清理内核调优配置 ----
echo ""
echo "[5/6] 清理内核调优配置..."
if [ -f /etc/security/limits.d/99-openfs.conf ]; then
    rm -f /etc/security/limits.d/99-openfs.conf
    echo "  已删除 /etc/security/limits.d/99-openfs.conf"
fi
if [ -f /etc/sysctl.d/99-openfs.conf ]; then
    rm -f /etc/sysctl.d/99-openfs.conf
    sysctl --system 2>/dev/null || true
    echo "  已删除 /etc/sysctl.d/99-openfs.conf"
fi

# ---- 6. 清理数据/配置/日志 (仅 --purge) ----
echo ""
echo "[6/6] 清理数据目录..."
if $PURGE; then
    # 配置
    if [ -d "$OPENFS_ETC" ]; then
        rm -rf "$OPENFS_ETC"
        echo "  已删除 $OPENFS_ETC"
    fi
    # 数据
    if [ -d "$OPENFS_VAR" ]; then
        rm -rf "$OPENFS_VAR"
        echo "  已删除 $OPENFS_VAR"
    fi
    # 日志
    if [ -d "$OPENFS_LOG" ]; then
        rm -rf "$OPENFS_LOG"
        echo "  已删除 $OPENFS_LOG"
    fi
    # 构建目录 (如果是源码部署)
    SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
    if [ -d "$SCRIPT_DIR/build" ]; then
        rm -rf "$SCRIPT_DIR/build"
        echo "  已删除 $SCRIPT_DIR/build"
    fi
else
    echo "  保留配置目录: $OPENFS_ETC"
    echo "  保留数据目录: $OPENFS_VAR"
    echo "  保留日志目录: $OPENFS_LOG"
    echo "  (使用 --purge 可完全清除)"
fi

echo ""
echo "============================================"
echo "  OpenFS 卸载完成!"
echo "============================================"
if ! $PURGE; then
    echo ""
    echo "  保留的文件:"
    echo "    配置: $OPENFS_ETC"
    echo "    数据: $OPENFS_VAR"
    echo "    日志: $OPENFS_LOG"
    echo ""
    echo "  如需完全清除, 运行:"
    echo "    sudo bash $0 --purge"
fi