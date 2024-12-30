#!/bin/bash

usage() {
    echo "Usage: $0 --db-path <database_path> --props-path <props_file_path>"
    exit 1
}

# 初始化变量
DATABASE_PATH=""
PROPS_FILE_PATH=""

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    key="$1"

    case $key in
        -d|--db-path)
        DATABASE_PATH="$2"
        shift # past argument
        shift # past value
        ;;
        -p|--props-path)
        PROPS_FILE_PATH="$2"
        shift # past argument
        shift # past value
        ;;
        -h|--help)
        usage
        ;;
        *)    # unknown option
        echo "Unknown option: $1"
        usage
        ;;
    esac
done

# 检查是否传入了必要的参数
if [ -z "$DATABASE_PATH" ] || [ -z "$PROPS_FILE_PATH" ]; then
    usage
fi

# 启动数据库
echo "Starting the database..."
gaussdb -D "$DATABASE_PATH" -M primary &
sleep 10  # 等待数据库完全启动

# node2：gs_ctl build -D /home/jyh/opengauss_data/ -b full -M standby -l logfile_mb_2

# 压数据
echo "Loading data into the database..."
cd "$PROPS_FILE_PATH"
./runDatabaseBuild.sh props.mot.jyh

# 运行Benchmark
echo "Running benchmark..."
./runBenchmark.sh props.mot.jyh

# 运行Benchmark
echo "Running benchmark..."
./runBenchmark.sh props.mot.jyh &

# 等待五分钟
echo "Waiting for 5 minutes..."
sleep 300

# 查找并强行kill掉用户名为jyh且进程名为gaussdb的进程
echo "Killing the database process..."
pkill -u jyh -f gaussdb

# 等待几秒钟以确保进程已被杀死
sleep 5

# 重新启动数据库
echo "Restarting the database..."
gaussdb -D "$DATABASE_PATH" -M primary &

# 确保新的数据库进程启动
sleep 10

# 确认新进程是否运行
if pgrep -u jyh -f gaussdb > /dev/null
then
    echo "Database restarted successfully!"
else
    echo "Failed to restart the database."
fi

echo "Benchmark run completed."

# 删除数据
echo "Database destroy..."
./runDatabaseDestroy.sh props.mot.jyh

# 查找并强行kill掉用户名为jyh且进程名为gaussdb的进程
echo "Killing the database process..."
pkill -u jyh -f gaussdb
