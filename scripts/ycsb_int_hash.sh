#!/bin/bash

cd ./P-CLHT
bash compile.sh lb
cd ..
mkdir ./build
cd ./build
rm -rf *
cmake -DCMAKE_CXX_FLAGS_RELEASE="-O0" -DCMAKE_C_COMPILER=/home/sjb/output/buildtools/gcc10.3/gcc/bin/gcc -DCMAKE_CXX_COMPILER=/home/sjb/output/buildtools/gcc10.3/gcc/bin/g++ ..
make -j
cd ..

for idx_type in clht levelhash cceh
do
    for workload in a b c
    do
        for nthreads in 1
        do
            ./build/ycsb ${idx_type} ${workload} randint uniform ${nthreads} &>> ./results/ycsb_int_hash.csv
            sleep 3
        done
        printf "\n\n" &>> ./results/ycsb_int_hash.csv
    done
done
