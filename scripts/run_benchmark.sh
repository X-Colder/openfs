#!/bin/bash
# ============================================================
# OpenFS Ubuntu 22.04 性能测试脚本
# 用法: bash run_benchmark.sh [--quick | --full | --endurance]
# ============================================================
set -uo pipefail

META_ADDR="${META_ADDR:-127.0.0.1:8100}"
DATA_ADDR="${DATA_ADDR:-127.0.0.1:8200}"
MODE="${1:---quick}"
RESULTS_DIR="benchmark_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

echo "============================================"
echo "  OpenFS 性能测试"
echo "  模式: $MODE"
echo "  Meta: $META_ADDR  Data: $DATA_ADDR"
echo "  结果: $RESULTS_DIR"
echo "============================================"

check_services() {
    echo "[检查] 验证服务..."
    if ! ss -tlnp 2>/dev/null | grep -q ":$(echo $META_ADDR | cut -d: -f2) "; then
        echo "  错误: MetaNode 未监听, 请先启动: sudo systemctl start openfs-meta"
        exit 1
    fi
    if ! ss -tlnp 2>/dev/null | grep -q ":$(echo $DATA_ADDR | cut -d: -f2) "; then
        echo "  错误: DataNode 未监听, 请先启动: sudo systemctl start openfs-data"
        exit 1
    fi
    echo "  服务正常"
}

disk_baseline() {
    echo ""
    echo "[基线] 磁盘性能测试..."
    local data_dir="/var/lib/openfs/data"
    local fio_file="${data_dir}/fio_testfile"

    fio --name=seq-write --filename="$fio_file" \
        --size=1G --bs=1M --ioengine=libaio --iodepth=64 \
        --rw=write --direct=1 --numjobs=1 \
        --group_reporting --runtime=30 \
        --output="${RESULTS_DIR}/disk_seq_write.json" --output-format=json 2>/dev/null || echo "  fio跳过"

    fio --name=seq-read --filename="$fio_file" \
        --size=1G --bs=1M --ioengine=libaio --iodepth=64 \
        --rw=read --direct=1 --numjobs=1 \
        --group_reporting --runtime=30 \
        --output="${RESULTS_DIR}/disk_seq_read.json" --output-format=json 2>/dev/null || true

    fio --name=rand-read --filename="$fio_file" \
        --size=1G --bs=4k --ioengine=libaio --iodepth=128 \
        --rw=randread --direct=1 --numjobs=4 \
        --group_reporting --runtime=30 \
        --output="${RESULTS_DIR}/disk_rand_read.json" --output-format=json 2>/dev/null || true

    rm -f "$fio_file" 2>/dev/null
    echo "  磁盘基线完成"
}

metadata_bench() {
    local total="$1"
    local concurrency="$2"
    echo ""
    echo "[元数据] 压测 (n=$total, c=$concurrency)..."

    if ! command -v ghz &>/dev/null; then
        echo "  ghz 未安装, 跳过"
        return
    fi

    echo "  CreateFsFile..."
    ghz --insecure --proto=proto/meta_service.proto \
        --call=openfs.MetaService/CreateFsFile \
        -d "{\"path\":\"/bench/file_{{.RequestNumber}}\",\"mode\":420,\"uid\":1000,\"gid\":1000,\"file_size\":4096}" \
        -n "$total" -c "$concurrency" \
        "$META_ADDR" 2>&1 | tee "${RESULTS_DIR}/meta_create_${concurrency}c.log"

    echo "  ReadDir..."
    ghz --insecure --proto=proto/meta_service.proto \
        --call=openfs.MetaService/ReadDir \
        -d '{"path":"/bench"}' \
        -n "$total" -c "$concurrency" \
        "$META_ADDR" 2>&1 | tee "${RESULTS_DIR}/meta_readdir_${concurrency}c.log"

    echo "  GetFileInfo..."
    ghz --insecure --proto=proto/meta_service.proto \
        --call=openfs.MetaService/GetFileInfo \
        -d '{"path":"/bench/file_1"}' \
        -n "$total" -c "$concurrency" \
        "$META_ADDR" 2>&1 | tee "${RESULTS_DIR}/meta_getfileinfo_${concurrency}c.log"
}

data_bench() {
    local total="$1"
    local concurrency="$2"
    echo ""
    echo "[数据] Block 读写压测 (n=$total, c=$concurrency)..."

    if ! command -v ghz &>/dev/null; then
        echo "  ghz 未安装, 跳过"
        return
    fi

    echo "  WriteBlock L0 (64KB)..."
    local DATA64=$(dd if=/dev/urandom bs=65536 count=1 2>/dev/null | base64 -w0)
    ghz --insecure --proto=proto/data_service.proto \
        --call=openfs.DataService/WriteBlock \
        -d "{\"block_id\":{{.RequestNumber}},\"crc32\":0,\"data\":\"$DATA64\"}" \
        -n "$total" -c "$concurrency" \
        "$DATA_ADDR" 2>&1 | tee "${RESULTS_DIR}/data_write_L0_${concurrency}c.log"

    echo "  ReadBlock..."
    ghz --insecure --proto=proto/data_service.proto \
        --call=openfs.DataService/ReadBlock \
        -d '{"block_id":0,"segment_id":1,"offset":4096}' \
        -n "$total" -c "$concurrency" \
        "$DATA_ADDR" 2>&1 | tee "${RESULTS_DIR}/data_read_${concurrency}c.log"
}

e2e_bench() {
    local file_count="$1"
    local file_size="$2"
    echo ""
    echo "[端到端] 文件读写压测 (count=$file_count, size=${file_size}B)..."

    local tmpdir="/tmp/openfs_bench_$$"
    mkdir -p "$tmpdir"
    dd if=/dev/urandom of="$tmpdir/testfile" bs=1 count="$file_size" 2>/dev/null

    local ok=0 fail=0
    local start_time=$(date +%s%N)

    for i in $(seq 1 "$file_count"); do
        if openfs_cli --meta "$META_ADDR" put "$tmpdir/testfile" "/bench/e2e_${i}.dat" &>/dev/null; then
            ok=$((ok + 1))
        else
            fail=$((fail + 1))
        fi
        if [ $((i % 100)) -eq 0 ]; then
            echo "  进度: $i/$file_count (ok=$ok fail=$fail)"
        fi
    done

    local end_time=$(date +%s%N)
    local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
    local elapsed_s=$(echo "scale=2; $elapsed_ms / 1000" | bc)
    local qps=$(echo "scale=1; $ok / $elapsed_s" | bc 2>/dev/null || echo "N/A")
    local throughput=$(echo "scale=1; $ok * $file_size / $elapsed_s / 1048576" | bc 2>/dev/null || echo "N/A")

    echo "  写入: ok=$ok fail=$fail 耗时=${elapsed_s}s QPS=$qps 吞吐=${throughput}MB/s"

    local read_ok=0 read_fail=0
    local rstart=$(date +%s%N)
    for i in $(seq 1 "$file_count"); do
        if openfs_cli --meta "$META_ADDR" get "/bench/e2e_${i}.dat" "$tmpdir/readback_${i}" &>/dev/null; then
            read_ok=$((read_ok + 1))
        else
            read_fail=$((read_fail + 1))
        fi
    done
    local rend=$(date +%s%N)
    local rtime=$(( (rend - rstart) / 1000000 ))
    local rtime_s=$(echo "scale=2; $rtime / 1000" | bc)
    local r_qps=$(echo "scale=1; $read_ok / $rtime_s" | bc 2>/dev/null || echo "N/A")

    echo "  读取: ok=$read_ok fail=$read_fail 耗时=${rtime_s}s QPS=$r_qps"

    cat > "${RESULTS_DIR}/e2e_${file_size}B.txt" << EOF
端到端: count=$file_count size=${file_size}B
写入: ok=$ok fail=$fail time=${elapsed_s}s QPS=$qps throughput=${throughput}MB/s
读取: ok=$read_ok fail=$read_fail time=${rtime_s}s QPS=$r_qps
EOF

    rm -rf "$tmpdir"
}

generate_report() {
    echo ""
    echo "============================================"
    echo "  性能测试报告"
    echo "============================================"
    echo "  结果目录: $RESULTS_DIR"
    echo ""
    for f in "${RESULTS_DIR}"/*.log; do
        if [ -f "$f" ]; then
            name=$(basename "$f")
            qps=$(grep -oP 'Requests/sec:\s*\K[\d.]+' "$f" 2>/dev/null || echo "N/A")
            p99=$(grep -oP '99%\s+\K[\d.]+' "$f" 2>/dev/null || echo "N/A")
            echo "  $name: QPS=$qps P99=${p99}ms"
        fi
    done
    for f in "${RESULTS_DIR}"/e2e_*.txt; do
        [ -f "$f" ] && cat "$f"
    done
}

# ============================================================
# 主流程
# ============================================================
check_services

case "$MODE" in
    --quick)
        echo "快速测试 (~5min)"
        disk_baseline
        metadata_bench 1000 8
        data_bench 1000 8
        e2e_bench 100 4096
        ;;
    --full)
        echo "完整测试 (~30min)"
        disk_baseline
        metadata_bench 10000 1
        metadata_bench 50000 16
        metadata_bench 100000 64
        data_bench 50000 8
        data_bench 100000 32
        e2e_bench 200 4096
        e2e_bench 100 65536
        e2e_bench 50 4194304
        ;;
    --endurance)
        echo "耐久测试 (Ctrl+C 停止)"
        iter=0
        while true; do
            iter=$((iter + 1))
            echo "[$(date '+%H:%M:%S')] 迭代 #$iter"
            metadata_bench 1000 8
            data_bench 500 8
            e2e_bench 10 65536
            local dn_pid=$(pgrep -f "data_node" | head -1)
            if [ -n "$dn_pid" ]; then
                echo "  DataNode PID=$dn_pid RSS=$(ps -o rss= -p $dn_pid | tr -d ' ')KB"
            fi
            sleep 60
        done
        ;;
    *)
        echo "用法: bash $0 [--quick | --full | --endurance]"
        exit 1
        ;;
esac

generate_report