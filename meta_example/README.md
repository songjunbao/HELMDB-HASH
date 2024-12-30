# BUILD

you should compile meta first

compile commands:
```sh
cmake .
cmake --build .
```

# TEST
run a meta server first:
```sh
./../meta/output/bin/Meta
```
then run the example client in another terminal
```sh
./example_project
```
you should see something like
```sh
[root@c4972756123e meta_example] ./example_project
I0704 06:49:59.010263 10080 main.cpp:23] Server id: 1
```