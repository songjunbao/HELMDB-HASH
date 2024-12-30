install dependencies
```sh
# make sure you have priviledge
yum upgrade
yum install -y gcc gcc-c++ make cmake git openssl-devel 
```

compile commands:
```sh
cmake .
cmake --build .
```