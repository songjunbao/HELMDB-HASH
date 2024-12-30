#!/usr/bin/env bash
SOCKET_NO=0

# 使用 --cpunodebind 和 --membind 替代过时的 -N 参数
numactl --cpunodebind=$SOCKET_NO --membind=$SOCKET_NO \
    sudo /home/song/nvm/pactree/TurboHash/release/hash_bench \
    --thread=8 \
    --benchmarks=load,readrandom \
    --stats_interval=10000\
    --read=500000 \
    --num=1000000 \
    --no_rehash=false \
    --report_interval=1 | tee load.data
