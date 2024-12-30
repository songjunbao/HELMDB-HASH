#!/bin/bash

rm -rf /mnt/pmem0/*
rm -rf /mnt/pmem1/*
sudo umount /mnt/pmem0
sudo umount /mnt/pmem1
sudo mount -o dax /dev/pmem0 /mnt/pmem0
sudo mount -o dax /dev/pmem1 /mnt/pmem1
cd /home/song/nvm/downloads/HELMDB

for idx_type in art
do
    for workload in a
    do
        for nthreads in 8
        do
            ./tmp_build/src/gausskernel/storage/nvmdb/ycsb_test/YCSBTests ${idx_type} ${workload} randint uniform ${nthreads} &>> ./results/ycsb_int_tree.csv
            sleep 3
        done
        printf "\n\n" &>> ./results/ycsb_int_tree.csv
    done
done

