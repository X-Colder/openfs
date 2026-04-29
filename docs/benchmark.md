# OpenFS 压力测试文档

## 一、测试目标

验证 OpenFS 在生产负载下的性能表现和稳定性，涵盖：

| 维度 | 目标 |
|------|------|
| 单节点吞吐 | 顺序写 ≥ 1GB/s，顺序读 ≥ 2GB/s（NVMe） |
| 元数据 OPS | CreateFile/GetFileInfo/ReadDir ≥ 10K ops/s（单 MetaNode） |
| 数据 IOPS | WriteBlock/ReadBlock ≥ 50K ops/s（单 DataNode，L0 Block） |
| 延迟 | 元数据 P99 < 5ms，数据读写 P99 < 10ms |
| 稳定性 | 7×24h 无内存泄漏、无崩溃、无数据损坏 |
| 线性扩展 | DataNode 吞吐随节点数线性增长 |

---

## 二、测试环境

### 2.1 推荐硬件配置

| 角色 | 数量 | CPU | 内存 | 磁盘 | 网络 |
|------|------|-----|------|------|------|
| MetaNode | 1~3 | 16 核 | 64GB | SSD 500GB | 10Gbps |
| DataNode | 1~6 | 32 核 | 128GB | NVMe 4TB × 2 | 10Gbps |
| 压测客户端 | 1~3 | 32 核 | 64GB | SSD 200GB | 10Gbps |

### 2.2 软件环境

| 组件 | 版本 |
|------|------|
| OS | RHEL 9.x / Ubuntu 22.04 |
| 内核 | 5.14+ |
| OpenFS | 1.0.0 Release |
| ghz (gRPC 压测工具) | 0.120+ |
| fio (磁盘基准) | 3.30+ |
| Python 3 | 3.9+ (压测脚本) |

### 2.3 网络拓扑

```
                     ┌─────────────┐
                     │  压测客户端  │
                     │ 10.0.2.100  │
                     └──────┬──────┘
                            │ 10Gbps
              ┌─────────────┼─────────────┐
              │             │             │
       ┌──────┴──────┐      │      ┌──────┴──────┐
       │  MetaNode   │      │      │  MetaNode   │
       │ 10.0.0.1    │      │      │ 10.0.0.2    │
       └─────────────┘      │      └─────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
 ┌──────┴──────┐    ┌──────┴──────┐    ┌──────┴──────┐
 │  DataNode1  │    │  DataNode2  │    │  DataNode3  │
 │ 10.0.1.1    │    │ 10.0.1.2    │    │ 10.0.1.3    │
 └─────────────┘    └─────────────┘    └─────────────┘
```

---

## 三、磁盘基线测试

在运行 OpenFS 压测前，先测量磁盘裸性能作为参考基线。

### 3.1 NVMe 顺序写基线

```bash
fio --name=nvme-seq-write \
    --filename=/data/openfs/fio_test \
    --size=10G --bs=1M --ioengine=libaio --iodepth=64 \
    --rw=write --direct=1 --numjobs=1 \
    --group_reporting --runtime=60
# 预期：≥ 2GB/s
```

### 3.2 NVMe 顺序读基线

```bash
fio --name=nvme-seq-read \
    --filename=/data/openfs/fio_test \
    --size=10G --bs=1M --ioengine=libaio --iodepth=64 \
    --rw=read --direct=1 --numjobs=1 \
    --group_reporting --runtime=60
# 预期：≥ 3GB/s
```

### 3.3 NVMe 随机读写基线

```bash
# 随机读 IOPS
fio --name=nvme-rand-read \
    --filename=/data/openfs/fio_test \
    --size=10G --bs=4k --ioengine=libaio --iodepth=128 \
    --rw=randread --direct=1 --numjobs=4 \
    --group_reporting --runtime=60
# 预期：≥ 500K IOPS

# 随机写 IOPS
fio --name=nvme-rand-write \
    --filename=/data/openfs/fio_test \
    --size=10G --bs=4k --ioengine=libaio --iodepth=128 \
    --rw=randwrite --direct=1 --numjobs=4 \
    --group_reporting --runtime=60
# 预期：≥ 400K IOPS
```

---

## 四、压测工具安装

### 4.1 ghz — gRPC 压测工具

```bash
# RHEL
sudo dnf install -y ghz 2>/dev/null || {
  GHZ_VER=0.120.0
  wget -q https://github.com/bojand/ghz/releases/download/v${GHZ_VER}/ghz_${GHZ_VER}_linux_x86_64.tar.gz
  sudo tar xzf ghz_${GHZ_VER}_linux_x86_64.tar.gz -C /usr/local/bin ghz
}

# Ubuntu
sudo snap install ghz 2>/dev/null || {
  GHZ_VER=0.120.0
  wget -q https://github.com/bojand/ghz/releases/download/v${GHZ_VER}/ghz_${GHZ_VER}_linux_x86_64.tar.gz
  sudo tar xzf ghz_${GHZ_VER}_linux_x86_64.tar.gz -C /usr/local/bin ghz
}

# 验证
ghz --version
```

### 4.2 自定义压测脚本

安装 Python 依赖：
```bash
pip3 install grpcio grpcio-tools
```

---

## 五、元数据压测

### 5.1 CreateFsFile — 文件创建 OPS

```bash
# 预热：先注册 DataNode
grpcurl -plaintext -import-path proto -proto node_service.proto \
  -d '{"address":"data1:50051","capacity":1073741824}' \
  meta1:50050 openfs.NodeService/Register

# 单线程基线
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/CreateFsFile \
  -d '{"path":"/bench/file_{{.RequestNumber}}","mode":420,"uid":1000,"gid":1000,"file_size":4096}' \
  -n 10000 -c 1 \
  meta1:50050

# 16 并发
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/CreateFsFile \
  -d '{"path":"/bench/file_{{.RequestNumber}}","mode":420,"uid":1000,"gid":1000,"file_size":4096}' \
  -n 100000 -c 16 \
  meta1:50050

# 64 并发
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/CreateFsFile \
  -d '{"path":"/bench/file_{{.RequestNumber}}","mode":420,"uid":1000,"gid":1000,"file_size":4096}' \
  -n 500000 -c 64 \
  meta1:50050
```

### 5.2 GetFileInfo — 文件查询 OPS

```bash
# 先创建一批文件供查询
for i in $(seq 1 1000); do
  grpcurl -plaintext -import-path proto -proto meta_service.proto \
    -d "{\"path\":\"/bench/lookup_$i\",\"mode\":420,\"uid\":1000,\"gid\":1000,\"file_size\":4096}" \
    meta1:50050 openfs.MetaService/CreateFsFile
done

# 查询压测
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/GetFileInfo \
  -d '{"path":"/bench/lookup_{{.RandomIntMin 1 1000}}"}' \
  -n 200000 -c 32 \
  meta1:50050
```

### 5.3 MkDir + ReadDir — 目录操作 OPS

```bash
# MkDir
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/MkDir \
  -d '{"path":"/bench_dir/dir_{{.RequestNumber}}","mode":493,"uid":1000,"gid":1000}' \
  -n 50000 -c 16 \
  meta1:50050

# ReadDir
ghz --insecure \
  --proto=proto/meta_service.proto \
  --call=openfs.MetaService/ReadDir \
  -d '{"path":"/bench_dir"}' \
  -n 100000 -c 32 \
  meta1:50050
```

### 5.4 元数据压测结果记录模板

| 场景 | 并发数 | 总请求数 | QPS | P50(ms) | P99(ms) | P99.9(ms) | 错误率 |
|------|--------|----------|-----|---------|---------|-----------|--------|
| CreateFsFile | 1 | 10K | | | | | |
| CreateFsFile | 16 | 100K | | | | | |
| CreateFsFile | 64 | 500K | | | | | |
| GetFileInfo | 32 | 200K | | | | | |
| MkDir | 16 | 50K | | | | | |
| ReadDir | 32 | 100K | | | | | |

---

## 六、数据读写压测

### 6.1 WriteBlock — 单节点写入吞吐

#### L0 Block (64KB) — IOPS 测试

```bash
# 生成 64KB 随机数据的 base64 编码
DATA64=$(dd if=/dev/urandom bs=65536 count=1 2>/dev/null | base64 -w0)

ghz --insecure \
  --proto=proto/data_service.proto \
  --call=openfs.DataService/WriteBlock \
  -d "{\"block_id\":{{.RequestNumber}},\"crc32\":0,\"data\":\"$DATA64\"}" \
  -n 100000 -c 32 \
  data1:50051
```

#### L2 Block (4MB) — 吞吐测试

```bash
# 生成 4MB 随机数据（写入临时文件，用 ghz 的 binary-data 模式）
python3 -c "
import os, grpc, sys
sys.path.insert(0, 'proto')
import data_service_pb2 as ds
import data_service_pb2_grpc as ds_grp

channel = grpc.insecure_channel('data1:50051')
stub = ds_grp.DataServiceStub(channel)

data_4m = os.urandom(4 * 1024 * 1024)
import struct
crc = 0  # 实际计算 CRC32

for i in range(500):
    req = ds.WriteBlockReq(block_id=i+1, crc32=crc, data=data_4m)
    resp = stub.WriteBlock(req)
    if resp.status != 0:
        print(f'Write failed at block {i}: status={resp.status}')
        break
    if (i+1) % 50 == 0:
        print(f'Written {i+1} blocks')
print('Done')
"
```

### 6.2 ReadBlock — 单节点读取吞吐

```bash
# 先写入一批 Block 供读取
# 然后用已知 segment_id + offset 进行读取压测

ghz --insecure \
  --proto=proto/data_service.proto \
  --call=openfs.DataService/ReadBlock \
  -d '{"block_id":0,"segment_id":1,"offset":4096}' \
  -n 100000 -c 32 \
  data1:50051
```

### 6.3 数据压测结果记录模板

| 场景 | Block 级别 | 数据大小 | 并发数 | QPS | 吞吐(MB/s) | P50(ms) | P99(ms) |
|------|-----------|----------|--------|-----|-----------|---------|---------|
| WriteBlock | L0 | 64KB | 32 | | | | |
| WriteBlock | L1 | 512KB | 16 | | | | |
| WriteBlock | L2 | 4MB | 8 | | | | |
| WriteBlock | L4 | 256MB | 1 | | | | |
| ReadBlock | L0 | 64KB | 32 | | | | |
| ReadBlock | L2 | 4MB | 8 | | | | |
| ReadBlock | L4 | 256MB | 1 | | | | |

---

## 七、端到端压测

### 7.1 Python 端到端压测脚本

```python
#!/usr/bin/env python3
"""
openfs_bench.py - OpenFS 端到端压测脚本

用法:
  python3 openfs_bench.py \
    --meta-addr meta1:50050 \
    --data-addr data1:50051 \
    --ops write --block-level L2 \
    --concurrency 16 --total 1000 \
    --data-size 4194304

  python3 openfs_bench.py \
    --meta-addr meta1:50050 \
    --data-addr data1:50051 \
    --ops read --concurrency 32 --total 5000
"""
import argparse
import grpc
import time
import threading
import os
import sys
import statistics
import json

# 需要先编译 proto 生成 Python 文件
# python3 -m grpc_tools.protoc -Iproto \
#   --python_out=. --grpc_python_out=. \
#   proto/common.proto proto/meta_service.proto \
#   proto/data_service.proto proto/node_service.proto

import common_pb2
import meta_service_pb2
import meta_service_pb2_grpc
import data_service_pb2
import data_service_pb2_grpc
import node_service_pb2
import node_service_pb2_grpc

BLOCK_SIZES = {
    'L0': 64 * 1024,
    'L1': 512 * 1024,
    'L2': 4 * 1024 * 1024,
    'L3': 32 * 1024 * 1024,
    'L4': 256 * 1024 * 1024,
}

BLOCK_LEVELS = {
    'L0': common_pb2.BLOCK_LEVEL_L0,
    'L1': common_pb2.BLOCK_LEVEL_L1,
    'L2': common_pb2.BLOCK_LEVEL_L2,
    'L3': common_pb2.BLOCK_LEVEL_L3,
    'L4': common_pb2.BLOCK_LEVEL_L4,
}


def compute_crc32(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def run_write_bench(meta_addr, data_addr, concurrency, total, block_level, data_size):
    """端到端写入压测: CreateFile -> AllocateBlock -> WriteBlock -> CommitBlock"""
    meta_ch = grpc.insecure_channel(meta_addr)
    meta_stub = meta_service_pb2_grpc.MetaServiceStub(meta_ch)
    data_ch = grpc.insecure_channel(data_addr)
    data_stub = data_service_pb2_grpc.DataServiceStub(data_ch)

    latencies = []
    errors = 0
    lock = threading.Lock()

    def write_one(idx):
        nonlocal errors
        t0 = time.perf_counter()
        try:
            # 1. CreateFile
            create_resp = meta_stub.CreateFsFile(meta_service_pb2.CreateFsFileReq(
                path=f'/bench/file_{idx}',
                mode=0o644, uid=1000, gid=1000,
                file_size=data_size
            ))
            if create_resp.status != 0:
                raise Exception(f'CreateFile failed: {create_resp.status}')
            inode_id = create_resp.inode.inode_id

            # 2. AllocateBlock
            alloc_resp = meta_stub.AllocateBlocks(meta_service_pb2.AllocBlocksReq(
                inode_id=inode_id,
                block_count=1,
                level=BLOCK_LEVELS[block_level]
            ))
            if alloc_resp.status != 0:
                raise Exception(f'AllocateBlocks failed: {alloc_resp.status}')

            # 3. WriteBlock
            data = os.urandom(data_size)
            crc = compute_crc32(data)
            block_id = alloc_resp.blocks[0].block_id
            write_resp = data_stub.WriteBlock(data_service_pb2.WriteBlockReq(
                block_id=block_id, crc32=crc, data=data
            ))
            if write_resp.status != 0:
                raise Exception(f'WriteBlock failed: {write_resp.status}')

            # 4. CommitBlock
            block_meta = alloc_resp.blocks[0]
            block_meta.segment_id = write_resp.segment_id
            block_meta.offset = write_resp.offset
            block_meta.crc32 = crc
            block_meta.size = data_size

            commit_resp = meta_stub.CommitBlocks(meta_service_pb2.CommitBlocksReq(
                inode_id=inode_id, blocks=[block_meta]
            ))
            if commit_resp.status != 0:
                raise Exception(f'CommitBlocks failed: {commit_resp.status}')

        except Exception as e:
            with lock:
                errors += 1
            return
        elapsed = (time.perf_counter() - t0) * 1000  # ms
        with lock:
            latencies.append(elapsed)

    # 执行压测
    print(f'开始写入压测: concurrency={concurrency}, total={total}, '
          f'block_level={block_level}, data_size={data_size}')
    t_start = time.perf_counter()

    threads = []
    for i in range(total):
        t = threading.Thread(target=write_one, args=(i,))
        threads.append(t)
        # 控制并发度
        while threading.active_count() - 1 >= concurrency:
            time.sleep(0.001)
        t.start()

    for t in threads:
        t.join()

    t_end = time.perf_counter()
    total_time = t_end - t_start

    # 输出结果
    if latencies:
        latencies.sort()
        qps = len(latencies) / total_time
        throughput_mb = (len(latencies) * data_size) / (1024 * 1024) / total_time
        print(f'\n===== 写入压测结果 =====')
        print(f'成功请求: {len(latencies)}/{total}')
        print(f'失败请求: {errors}')
        print(f'总耗时:   {total_time:.2f}s')
        print(f'QPS:      {qps:.1f} ops/s')
        print(f'吞吐量:   {throughput_mb:.1f} MB/s')
        print(f'延迟 P50: {latencies[len(latencies)//2]:.2f} ms')
        print(f'延迟 P99: {latencies[int(len(latencies)*0.99)]:.2f} ms')
        print(f'延迟 P99.9: {latencies[int(len(latencies)*0.999)]:.2f} ms')
        print(f'延迟 Max: {latencies[-1]:.2f} ms')


def run_read_bench(meta_addr, data_addr, concurrency, total, inode_ids):
    """端到端读取压测: GetFileInfo -> GetBlockLocations -> ReadBlock"""
    meta_ch = grpc.insecure_channel(meta_addr)
    meta_stub = meta_service_pb2_grpc.MetaServiceStub(meta_ch)
    data_ch = grpc.insecure_channel(data_addr)
    data_stub = data_service_pb2_grpc.DataServiceStub(data_ch)

    latencies = []
    errors = 0
    lock = threading.Lock()
    file_idx = [0]

    def read_one():
        nonlocal errors
        with lock:
            idx = file_idx[0] % len(inode_ids)
            file_idx[0] += 1
        inode_id = inode_ids[idx]
        t0 = time.perf_counter()
        try:
            # 1. GetBlockLocations
            locs_resp = meta_stub.GetBlockLocations(
                meta_service_pb2.GetBlockLocsReq(inode_id=inode_id))
            if locs_resp.status != 0:
                raise Exception(f'GetBlockLocations failed: {locs_resp.status}')

            # 2. ReadBlock for each block
            for blk in locs_resp.blocks:
                read_resp = data_stub.ReadBlock(data_service_pb2.ReadBlockReq(
                    segment_id=blk.segment_id,
                    offset=blk.offset,
                    size=blk.size
                ))
                if read_resp.status != 0:
                    raise Exception(f'ReadBlock failed: {read_resp.status}')
                # CRC 验证
                if read_resp.crc32 != blk.crc32:
                    raise Exception('CRC mismatch!')

        except Exception as e:
            with lock:
                errors += 1
            return
        elapsed = (time.perf_counter() - t0) * 1000
        with lock:
            latencies.append(elapsed)

    t_start = time.perf_counter()
    threads = []
    for i in range(total):
        t = threading.Thread(target=read_one)
        threads.append(t)
        while threading.active_count() - 1 >= concurrency:
            time.sleep(0.001)
        t.start()

    for t in threads:
        t.join()

    t_end = time.perf_counter()
    total_time = t_end - t_start

    if latencies:
        latencies.sort()
        qps = len(latencies) / total_time
        print(f'\n===== 读取压测结果 =====')
        print(f'成功请求: {len(latencies)}/{total}')
        print(f'失败请求: {errors}')
        print(f'总耗时:   {total_time:.2f}s')
        print(f'QPS:      {qps:.1f} ops/s')
        print(f'延迟 P50: {latencies[len(latencies)//2]:.2f} ms')
        print(f'延迟 P99: {latencies[int(len(latencies)*0.99)]:.2f} ms')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='OpenFS Benchmark')
    parser.add_argument('--meta-addr', required=True, help='MetaNode address')
    parser.add_argument('--data-addr', required=True, help='DataNode address')
    parser.add_argument('--ops', choices=['write', 'read'], required=True)
    parser.add_argument('--concurrency', type=int, default=16)
    parser.add_argument('--total', type=int, default=1000)
    parser.add_argument('--block-level', default='L2', choices=BLOCK_SIZES.keys())
    parser.add_argument('--data-size', type=int, default=None)
    parser.add_argument('--inode-ids', type=str, default=None,
                        help='Comma-separated inode IDs for read bench')
    args = parser.parse_args()

    if args.data_size is None:
        args.data_size = BLOCK_SIZES[args.block_level]

    if args.ops == 'write':
        run_write_bench(args.meta_addr, args.data_addr,
                        args.concurrency, args.total,
                        args.block_level, args.data_size)
    elif args.ops == 'read':
        if not args.inode_ids:
            print('Read bench requires --inode-ids')
            sys.exit(1)
        inode_ids = [int(x) for x in args.inode_ids.split(',')]
        run_read_bench(args.meta_addr, args.data_addr,
                       args.concurrency, args.total, inode_ids)
```

### 7.2 执行端到端压测

```bash
# 编译 proto 生成 Python 文件
python3 -m grpc_tools.protoc -Iproto \
  --python_out=. --grpc_python_out=. \
  proto/common.proto proto/meta_service.proto \
  proto/data_service.proto proto/node_service.proto

# 写入压测：L2 Block (4MB) × 1000 次 × 16 并发
python3 openfs_bench.py \
  --meta-addr meta1:50050 \
  --data-addr data1:50051 \
  --ops write --block-level L2 \
  --concurrency 16 --total 1000

# 写入压测：L0 Block (64KB) × 10000 次 × 32 并发
python3 openfs_bench.py \
  --meta-addr meta1:50050 \
  --data-addr data1:50051 \
  --ops write --block-level L0 --data-size 65536 \
  --concurrency 32 --total 10000

# 读取压测（使用写入时输出的 inode_id）
python3 openfs_bench.py \
  --meta-addr meta1:50050 \
  --data-addr data1:50051 \
  --ops read --concurrency 32 --total 5000 \
  --inode-ids 1001,1002,1003,...,2000
```

---

## 八、多节点扩展性压测

### 8.1 水平扩展测试方案

固定压测客户端数量，逐步增加 DataNode 节点，验证吞吐线性增长。

| DataNode 数量 | 预期写入吞吐 | 预期读取吞吐 |
|--------------|-------------|-------------|
| 1 | 1GB/s | 2GB/s |
| 2 | 2GB/s | 4GB/s |
| 3 | 3GB/s | 6GB/s |
| 6 | 6GB/s | 12GB/s |

### 8.2 执行方式

```bash
# Step 1: 启动 1 个 DataNode，运行基准压测
python3 openfs_bench.py --ops write --concurrency 16 --total 2000 \
  --meta-addr meta1:50050 --data-addr data1:50051

# Step 2: 添加第 2 个 DataNode，运行同样压测
# DataNode2 启动后会自动注册到 MetaNode
python3 openfs_bench.py --ops write --concurrency 32 --total 4000 \
  --meta-addr meta1:50050 --data-addr data1:50051

# Step 3: 添加第 3 个 DataNode...
# 重复直到目标节点数
```

> 注意：当前版本 `BlockAllocator` 的负载均衡策略会自动将新 Block 分配到新节点。
> 扩展性测试时，应使用新文件（新 inode）进行写入，已有文件的 Block 不会自动迁移。

---

## 九、长时间稳定性测试

### 9.1 7×24h 混合负载测试

模拟真实生产负载：70% 读 + 20% 写 + 10% 元数据操作。

```bash
#!/bin/bash
# stability_test.sh - 7×24h 稳定性测试
META=meta1:50050
DATA=data1:50051
DURATION_HOURS=168  # 7 天
START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION_HOURS * 3600))
ITER=0

while [ $(date +%s) -lt $END_TIME ]; do
    ITER=$((ITER + 1))
    NOW=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$NOW] Stability iteration $ITER"

    # 写入 100 个 L2 Block
    python3 openfs_bench.py --ops write --block-level L2 \
      --concurrency 8 --total 100 \
      --meta-addr $META --data-addr $DATA

    # 元数据操作
    ghz --insecure --proto=proto/meta_service.proto \
      --call=openfs.MetaService/ReadDir \
      -d '{"path":"/"}' -n 1000 -c 8 $META

    # 检查进程状态
    for host in meta1 data1 data2 data3; do
        ssh $host "systemctl is-active openfs-meta openfs-data 2>/dev/null" || \
          echo "ALERT: Service down on $host"
    done

    # 检查内存使用（RSS）
    for host in data1 data2 data3; do
        RSS=$(ssh $host "ps -o rss= -p $(cat /opt/openfs/run/data_node.pid 2>/dev/null)" 2>/dev/null)
        echo "  $host DataNode RSS: ${RSS:-N/A} KB"
    done

    # 每 10 轮输出磁盘使用
    if [ $((ITER % 10)) -eq 0 ]; then
        for host in data1 data2 data3; do
            ssh $host "df -h /data/openfs"
        done
    fi

    sleep 60  # 每轮间隔 1 分钟
done

echo "Stability test completed after $ITER iterations"
```

### 9.2 内存泄漏检测

```bash
# 在稳定性测试期间，每 5 分钟采集一次内存信息
while true; do
    for host in data1 data2 data3; do
        PID=$(ssh $host "cat /opt/openfs/run/data_node.pid 2>/dev/null")
        if [ -n "$PID" ]; then
            RSS=$(ssh $host "ps -o rss= -p $PID" 2>/dev/null)
            VSZ=$(ssh $host "ps -o vsz= -p $PID" 2>/dev/null)
            echo "$(date +%s) $host RSS=$RSS VSZ=$VSZ" >> mem_trace.log
        fi
    done
    sleep 300
done

# 测试结束后，分析内存趋势
python3 -c "
import re
data = {}
with open('mem_trace.log') as f:
    for line in f:
        parts = line.strip().split()
        ts, host = parts[0], parts[1]
        rss = int(parts[2].split('=')[1])
        data.setdefault(host, []).append((int(ts), rss))

for host, points in data.items():
    if len(points) < 10:
        continue
    first_rss = points[0][1]
    last_rss = points[-1][1]
    growth_mb = (last_rss - first_rss) / 1024
    hours = (points[-1][0] - points[0][0]) / 3600
    rate_mb_h = growth_mb / hours if hours > 0 else 0
    print(f'{host}: RSS {first_rss//1024}MB -> {last_rss//1024}MB '
          f'({growth_mb:+.1f}MB, {rate_mb_h:+.2f}MB/h)')
    if rate_mb_h > 5:
        print(f'  WARNING: 内存增长速率 {rate_mb_h:.1f}MB/h 超过阈值 5MB/h')
"
```

### 9.3 数据完整性校验

```bash
# 稳定性测试结束后，校验所有写入 Block 的 CRC32
python3 -c "
import grpc, sys
import meta_service_pb2_grpc, meta_service_pb2
import data_service_pb2_grpc, data_service_pb2
import common_pb2

meta_ch = grpc.insecure_channel('meta1:50050')
meta_stub = meta_service_pb2_grpc.MetaServiceStub(meta_ch)
data_ch = grpc.insecure_channel('data1:50051')
data_stub = data_service_pb2_grpc.DataServiceStub(data_ch)

# 遍历 /bench 目录下的文件
read_resp = meta_stub.ReadDir(meta_service_pb2.ReadDirReq(path='/bench'))
total, ok, fail = 0, 0, 0

for entry in read_resp.entries:
    if entry.file_type != common_pb2.FILE_TYPE_REGULAR:
        continue
    info = meta_stub.GetFileInfo(meta_service_pb2.GetFileInfoReq(path=f'/bench/{entry.name}'))
    locs = meta_stub.GetBlockLocations(meta_service_pb2.GetBlockLocsReq(inode_id=info.inode.inode_id))
    for blk in locs.blocks:
        total += 1
        resp = data_stub.ReadBlock(data_service_pb2.ReadBlockReq(
            segment_id=blk.segment_id, offset=blk.offset, size=blk.size))
        if resp.status == 0 and resp.crc32 == blk.crc32:
            ok += 1
        else:
            fail += 1
            print(f'CRC MISMATCH: file={entry.name} block={blk.block_id} '
                  f'expected_crc={blk.crc32} actual_crc={resp.crc32}')

print(f'完整性校验: 总计={total} 通过={ok} 失败={fail}')
if fail > 0:
    sys.exit(1)
"
```

---

## 十、故障注入测试

### 10.1 DataNode 进程杀灭恢复

```bash
# 1. 正常写入一批数据
python3 openfs_bench.py --ops write --block-level L2 \
  --concurrency 8 --total 500 --meta-addr meta1:50050 --data-addr data1:50051

# 2. 杀掉 DataNode 进程
ssh data1 "sudo systemctl kill openfs-data"

# 3. 验证 MetaNode 检测到节点下线（查看日志）
journalctl -u openfs-meta --since '30 sec ago' | grep -i 'offline\|suspect'

# 4. 重启 DataNode
ssh data1 "sudo systemctl start openfs-data"

# 5. 验证 DataNode 重新注册
sleep 10
journalctl -u openfs-data --since '30 sec ago' | grep 'Registered'

# 6. 验证已有数据可读（Segment 文件持久化在磁盘上）
python3 openfs_bench.py --ops read --concurrency 8 --total 100 \
  --meta-addr meta1:50050 --data-addr data1:50051 \
  --inode-ids <之前写入的inode列表>
```

### 10.2 网络分区模拟

```bash
# 模拟 DataNode 网络隔离
sudo iptables -A OUTPUT -d 10.0.0.1 -j DROP  # 屏蔽到 MetaNode 的流量

# 等待心跳超时
sleep 30

# 验证 MetaNode 标记节点 OFFLINE
journalctl -u openfs-meta --since '1 min ago' | grep -i 'offline'

# 恢复网络
sudo iptables -D OUTPUT -d 10.0.0.1 -j DROP

# 验证心跳恢复
sleep 10
journalctl -u openfs-data --since '30 sec ago' | grep 'Heartbeat'
```

### 10.3 磁盘空间耗尽

```bash
# 填充数据盘至 95%
dd if=/dev/zero of=/data/openfs/fill_file bs=1M count=$(df --output=avail /data/openfs | tail -1 | awk '{print int($1 * 0.95 / 1024)}')

# 尝试写入，应返回 kNoSpace
grpcurl -plaintext -import-path proto -proto data_service.proto \
  -d '{"block_id":99999,"crc32":0,"data":"AAAA..."}' \
  data1:50051 openfs.DataService/WriteBlock

# 清理
rm /data/openfs/fill_file
```

---

## 十一、性能调优参考

### 11.1 gRPC 通道参数

| 参数 | 默认值 | 建议值 | 说明 |
|------|--------|--------|------|
| GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH | 4MB | 256MB | L4 Block 需要 |
| GRPC_ARG_MAX_SEND_MESSAGE_LENGTH | 4MB | 256MB | 同上 |
| GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS | 30000 | 60000 | 避免长连接误杀 |
| GRPC_ARG_KEEPALIVE_TIME_MS | 7200000 | 30000 | 心跳间隔 |
| GRPC_ARG_KEEPALIVE_TIMEOUT_MS | 20000 | 10000 | 心跳超时 |

### 11.2 DataNode 调优

| 参数 | 调优方向 |
|------|----------|
| segment_size | NVMe: 1GB, HDD: 256MB |
| DataNode 写线程数 | 与 CPU 核数相当 |
| Segment 文件对齐 | 4KB 边界对齐 |
| 文件打开模式 | O_DIRECT + O_DSYNC (降低 50% 写入延迟) |

---

## 十二、压测报告模板

```
============================
OpenFS 压测报告
============================
测试日期:       YYYY-MM-DD
测试人员:       
版本号:         v1.0.0
构建类型:       Release
OS:             RHEL 9.2 / Ubuntu 22.04
内核:           5.14.0

一、测试环境
  MetaNode:  x 台,  CPU, 内存, 磁盘
  DataNode:  x 台,  CPU, 内存, NVMe
  压测客户端: x 台,  CPU, 内存

二、磁盘基线
  顺序写:  xxx MB/s
  顺序读:  xxx MB/s
  随机读 IOPS:  xxx

三、元数据性能
  CreateFsFile:  QPS=xxx, P99=xxx ms
  GetFileInfo:   QPS=xxx, P99=xxx ms
  ReadDir:       QPS=xxx, P99=xxx ms

四、数据读写性能
  WriteBlock L0:  QPS=xxx, 吞吐=xxx MB/s, P99=xxx ms
  WriteBlock L2:  QPS=xxx, 吞吐=xxx MB/s, P99=xxx ms
  ReadBlock  L0:  QPS=xxx, 吞吐=xxx MB/s, P99=xxx ms
  ReadBlock  L2:  QPS=xxx, 吞吐=xxx MB/s, P99=xxx ms

五、扩展性
  1 DataNode:  写=xxx MB/s, 读=xxx MB/s
  3 DataNode:  写=xxx MB/s, 读=xxx MB/s
  6 DataNode:  写=xxx MB/s, 读=xxx MB/s

六、稳定性
  7x24h 内存增长:  +xxx MB (xxx MB/h)
  CRC 校验:        通过/失败
  崩溃次数:        0

七、故障恢复
  DataNode 重启恢复时间:  xxx s
  数据完整性:  通过/失败

八、结论与建议
  ...
============================
```