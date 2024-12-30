# 确保路径正确
db_test(${PROJECT_SOURCE_DIR}/src/test/spinlock_test.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/hash_test.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/example.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/turbo_hash_test.cc)

db_exe(${PROJECT_SOURCE_DIR}/src/test/hash_bench.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/hash_bench_pmdk.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/hash_bench_30.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/hash_bench_pmdk_30.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/turbo_hash_test.cc)
db_exe(${PROJECT_SOURCE_DIR}/src/test/motivation.cc)

# Pmem hash test
db_test(${PROJECT_SOURCE_DIR}/src/test/ralloc_test.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/hello_libpmem.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/pmem_rehash_test.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/pmem_hash_test.cc)
db_test(${PROJECT_SOURCE_DIR}/src/test/pmem_hash_recover_test.cc)
