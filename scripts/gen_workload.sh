#!/bin/bash

source scripts/compile_config.sh

sudo apt-get -y install build-essential cmake libboost-all-dev libpapi-dev default-jdk
sudo apt-get -y install libtbb-dev libjemalloc-dev

cd ./index-microbench
mkdir ./workloads
bash generate_all_workloads.sh
cd ..

mkdir ./results

cd ./P-CLHT
bash compile.sh lb
cd ..
mkdir ./build
