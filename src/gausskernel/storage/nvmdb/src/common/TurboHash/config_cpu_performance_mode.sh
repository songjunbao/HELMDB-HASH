#!/usr/bin/env bash

# 设置 CPU 调节模式
cmd="frequency-set --governor performance"

# 获取最大 CPU 数
MAX_CPU=$((`nproc --all` - 1))

# 遍历每个 CPU 核心，应用设置
for i in $(seq 0 $MAX_CPU);
do
    echo "Changing CPU $i with parameter $cmd"
    
    # 使用 cpupower 设置每个 CPU 核心的频率调节模式
    sudo cpupower -c $i $cmd
done
