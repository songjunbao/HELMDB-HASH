#!/usr/bin/env bash
SOCKET_NO=0

# Insert 120 milliion and, each thread read 10 million

t=16

numactl -N $SOCKET_NO sudo ../release/hash_bench_pmdk  --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=100000 --num=1209600 --bucket_count=65536 --cell_count=16 --no_rehash=false | tee ycsb.turbo

numactl -N $SOCKET_NO sudo ../release/hash_bench_pmdk_30  --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf   --read=100000 --num=1209600 --bucket_count=65536 --cell_count=16 --no_rehash=false | tee ycsb.turbo30

#numactl -N $SOCKET_NO sudo ../Dash/release/ycsb_bench  --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=10000000 --num=120960000 | tee ycsb.dash
#
#numactl -N $SOCKET_NO sudo ../CCEH-PMDK/ycsb_bench --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=100000 --num=1209600 | tee ycsb.cceh

#numactl -N $SOCKET_NO sudo ../Clevel-Hashing/release/tests/ycsb_bench_cceh30 --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=100000 --num=1209600 | tee ycsb.cceh30
#
#numactl -N $SOCKET_NO sudo ../Clevel-Hashing/release/tests/ycsb_bench_clevel30 --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=100000 --num=1209600 | tee ycsb.clevel30
#
#numactl -N $SOCKET_NO sudo ../Clevel-Hashing/release/tests/ycsb_bench_clht30 --thread=$t --benchmarks=load,ycsbd,ycsba,ycsbb,ycsbc,ycsbf  --read=100000 --num=1209600 | tee ycsb.clht30






