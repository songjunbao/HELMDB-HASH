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

for idx_type in woart
do
    for workload in a b c
    do
        for nthreads in 16
        do
            ./build/ycsb ${idx_type} ${workload} randint uniform ${nthreads} &>> ./results/ycsb_int_tree.csv
            sleep 3
        done
        printf "\n\n" &>> ./results/ycsb_int_tree.csv
    done
done

cd ./build
rm -rf *
cmake -DWOART_STRING=ON -DCMAKE_CXX_FLAGS_RELEASE="-O0" -DCMAKE_C_COMPILER=/home/sjb/output/buildtools/gcc10.3/gcc/bin/gcc -DCMAKE_CXX_COMPILER=/home/sjb/output/buildtools/gcc10.3/gcc/bin/g++ ..
make -j
cd ..

for idx_type in woart
do
    for workload in a b c
    do
        for nthreads in 16
        do
            ./build/ycsb ${idx_type} ${workload} string uniform ${nthreads} &>> ./results/ycsb_str_tree.csv
            sleep 3
        done
        printf "\n\n" &>> ./results/ycsb_str_tree.csv
    done
done
