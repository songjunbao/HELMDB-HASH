#include <iostream>
#include <chrono>
#include <random>
#include <cstring>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <functional> // for std::hash
#include "../include/common/pactree/pactree.h"
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/global_control.h> // 如果需要控制并行度
#include <glog/logging.h>
#include <cstdlib>  // 引入 system 函数需要的头文件
#include <experimental/filesystem>
#include "nvmdb_thread.h"
//#define STRINGKEY
using namespace NVMDB;
using namespace std;
// index types
enum {
    TYPE_ART,
    TYPE_HOT,
    TYPE_BWTREE,
    TYPE_MASSTREE,
    TYPE_CLHT,
    TYPE_FASTFAIR,
    TYPE_LEVELHASH,
    TYPE_CCEH,
    TYPE_WOART,
};

enum {
    OP_INSERT,
    OP_UPDATE,
    OP_READ,
    OP_SCAN,
    OP_DELETE,
};

enum {
    WORKLOAD_A,
    WORKLOAD_B,
    WORKLOAD_C,
    WORKLOAD_D,
    WORKLOAD_E,
};

enum {
    RANDINT_KEY,
    STRING_KEY,
};

enum {
    UNIFORM,
    ZIPFIAN,
};

namespace Dummy {
    inline void mfence() {asm volatile("mfence":::"memory");}

    inline void clflush(char *data, int len, bool front, bool back)
    {
        if (front)
            mfence();
        volatile char *ptr = (char *)((unsigned long)data & ~(64 - 1));
        for (; ptr < data+len; ptr += 64){
#ifdef CLFLUSH
            asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));
#elif CLFLUSH_OPT
            asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)(ptr)));
#elif CLWB
            asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(ptr)));
#endif
        }
        if (back)
            mfence();
    }
}



static uint64_t LOAD_SIZE = 640000;
static uint64_t RUN_SIZE = 6400000;

void loadKey(TID tid, Key &key) {
    return ;
}
#ifdef STRINGKEY
void ycsb_load_run_string(int index_type, int wl, int kt, int ap, int num_thread,
        std::vector<string> &init_keys,
        std::vector<string> &keys,
        std::vector<uint64_t> &init_vals,
        std::vector<uint64_t> &vals,
        std::vector<int> &ranges,
        std::vector<int> &ops)
{
    fprintf(stderr, "STRINGKEY11111111\n");
    std::string init_file;
    std::string txn_file;


    if (ap == UNIFORM) {
        if (kt == STRING_KEY && wl == WORKLOAD_A) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_load_workloada";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_run_workloada";
        } else if (kt == STRING_KEY && wl == WORKLOAD_B) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_load_workloadb";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_run_workloadb";
        } else if (kt == STRING_KEY && wl == WORKLOAD_C) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_load_workloadc";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_run_workloadc";
        } else if (kt == STRING_KEY && wl == WORKLOAD_D) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_load_workloadd";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_run_workloadd";
        } else if (kt == STRING_KEY && wl == WORKLOAD_E) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_load_workloade";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/ycsbkey_run_workloade";
        }
    } else {
        if (kt == STRING_KEY && wl == WORKLOAD_A) {
            init_file = "./index-microbench/workloads/ycsbkey_load_workloada";
            txn_file = "./index-microbench/workloads/ycsbkey_run_workloada";
        } else if (kt == STRING_KEY && wl == WORKLOAD_B) {
            init_file = "./index-microbench/workloads/ycsbkey_load_workloadb";
            txn_file = "./index-microbench/workloads/ycsbkey_run_workloadb";
        } else if (kt == STRING_KEY && wl == WORKLOAD_C) {
            init_file = "./index-microbench/workloads/ycsbkey_load_workloadc";
            txn_file = "./index-microbench/workloads/ycsbkey_run_workloadc";
        } else if (kt == STRING_KEY && wl == WORKLOAD_D) {
            init_file = "./index-microbench/workloads/ycsbkey_load_workloadd";
            txn_file = "./index-microbench/workloads/ycsbkey_run_workloadd";
        } else if (kt == STRING_KEY && wl == WORKLOAD_E) {
            init_file = "./index-microbench/workloads/ycsbkey_load_workloade";
            txn_file = "./index-microbench/workloads/ycsbkey_run_workloade";
        }
    }

    std::ifstream infile_load(init_file);

    std::string op;
    std::string key;
    int range;

    std::string insert("INSERT");
    std::string update("UPDATE");
    std::string read("READ");
    std::string scan("SCAN");
    std::string maxKey("z");

    int count = 0;
    int len_5_count = 0;//统计len>5的键值数量
    uint64_t val;
    while ((count < LOAD_SIZE) && infile_load.good()) {
        infile_load >> op >> key;
        if (op.compare(insert) != 0) {
            std::cout << "READING LOAD FILE FAIL!\n";
            return ;
        }
        // 使用 std::hash 生成哈希值
        size_t hash_value = std::hash<string>{}(key);
        // 将 size_t 转换为 uint64_t
        uint64_t key_val = static_cast<uint64_t>(hash_value);
        init_keys.push_back(key);
        init_vals.push_back(key_val);
        count++;
    }

    fprintf(stderr, "Loaded %d keys\n", count);

    std::ifstream infile_txn(txn_file);

    count = 0;
    while ((count < RUN_SIZE) && infile_txn.good()) {
        infile_txn >> op >> key;
        if (op.compare(insert) == 0) {
            ops.push_back(OP_INSERT);
            // 使用 std::hash 生成哈希值
            size_t hash_value = std::hash<string>{}(key);
            // 将 size_t 转换为 uint64_t
            uint64_t key_val = static_cast<uint64_t>(hash_value);
            keys.push_back(key);
            vals.push_back(key_val);
            ranges.push_back(1);
        } else if (op.compare(update) == 0) {
            ops.push_back(OP_UPDATE);
            // 使用 std::hash 生成哈希值
            size_t hash_value = std::hash<string>{}(key);
            // 将 size_t 转换为 uint64_t
            uint64_t key_val = static_cast<uint64_t>(hash_value);
            keys.push_back(key);
            vals.push_back(key_val);
            ranges.push_back(1);
        } else if (op.compare(read) == 0) {
            ops.push_back(OP_READ);
            // 使用 std::hash 生成哈希值
            size_t hash_value = std::hash<string>{}(key);
            // 将 size_t 转换为 uint64_t
            uint64_t key_val = static_cast<uint64_t>(hash_value);
            keys.push_back(key);
            vals.push_back(key_val);
            ranges.push_back(1);
        } else if (op.compare(scan) == 0) {
            infile_txn >> range;
            ops.push_back(OP_SCAN);
            // 使用 std::hash 生成哈希值
            size_t hash_value = std::hash<string>{}(key);
            // 将 size_t 转换为 uint64_t
            uint64_t key_val = static_cast<uint64_t>(hash_value);
            keys.push_back(key);
            vals.push_back(key_val);
            ranges.push_back(range);
        } else {
            std::cout << "UNRECOGNIZED CMD!\n";
            return;
        }
        count++;
    }

    if (index_type == TYPE_ART) {
        auto *pt = new PACTree();

        {
            // Load
            auto starttime = std::chrono::system_clock::now();
            tbb::parallel_for(tbb::blocked_range<uint64_t>(0, LOAD_SIZE), [&](const tbb::blocked_range<uint64_t> &scope) {
                pt->registerThread();
                for (uint64_t i = scope.begin(); i != scope.end(); i++) {
                    Key_t key(init_keys[i].c_str());
                    Key k;
                    k.Set(key.getData(), key.keyLength);
                    if(k.GetKeyLen() > 5){
                        len_5_count++;
                    }
                    pt->Insert(key, init_vals[i]);
                }
            });
            fprintf(stderr, "key len > 5:%d \n", len_5_count);
            len_5_count = 0;
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now() - starttime);
            printf("Throughput: load, %f ,ops/us\n", (LOAD_SIZE * 1.0) / duration.count());
        }

        {
            // Run
            auto starttime = std::chrono::system_clock::now();
            tbb::parallel_for(tbb::blocked_range<uint64_t>(0, RUN_SIZE), [&](const tbb::blocked_range<uint64_t> &scope) {
                pt->registerThread();
                for (uint64_t i = scope.begin(); i != scope.end(); i++) {
                    Key *key;
                    if (ops[i] == OP_INSERT) {
                        Key_t key(keys[i].c_str());
                        pt->Insert(key, vals[i]);
                    } else if (ops[i] == OP_READ) {
                        bool found;
                        Key_t key(keys[i].c_str());
                        auto val = pt->lookup(key, &found);
                        if (val != vals[i]) {
                            std::cout << "[ART] wrong key read: " << val << " expected:" << vals[i] << std::endl;
                            throw;
                        }
                    } else if (ops[i] == OP_SCAN) {
                        LookupSnapshot snapshot{};
                        std::vector<std::pair<Key_t, Val_t>> result;
                        /* case 1: 按照顺序访问都能访问到 */
                        Key_t start(keys[i].c_str());
                        Key_t end(keys[i + ranges[i]].c_str());
                        size_t resultsSize = ranges[i];
                        pt->scan(start, end, resultsSize, snapshot, false, result);
                    } else if (ops[i] == OP_UPDATE) {
                        std::cout << "NOT SUPPORTED CMD!\n";
                        exit(0);
                    }
                }
                pt->unregisterThread();
            });
            fprintf(stderr, "成功执行操作\n");
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now() - starttime);
            printf("Throughput: run, %f ,ops/us\n", (RUN_SIZE * 1.0) / duration.count());
        }
        delete pt;
    }
}
#else
void ycsb_load_run_randint(int index_type, int wl, int kt, int ap, int num_thread,
        std::vector<uint64_t> &init_keys,
        std::vector<uint64_t> &keys,
        std::vector<int> &ranges,
        std::vector<int> &ops)
{
    std::string init_file;
    std::string txn_file;
    if (ap == UNIFORM) {
        if (kt == RANDINT_KEY && wl == WORKLOAD_A) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loada_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsa_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_B) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadb_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsb_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_C) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadc_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsc_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_D) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadd_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsd_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_E) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loade_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnse_unif_int.dat";
        }
    } else {
        if (kt == RANDINT_KEY && wl == WORKLOAD_A) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loada_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsa_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_B) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadb_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsb_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_C) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadc_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsc_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_D) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loadd_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnsd_unif_int.dat";
        } else if (kt == RANDINT_KEY && wl == WORKLOAD_E) {
            init_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/loade_unif_int.dat";
            txn_file = "/home/song/nvm/downloads/HELMDB/src/gausskernel/storage/nvmdb/ycsb_test/index-microbench/workloads/txnse_unif_int.dat";
        }
    }

    std::ifstream infile_load(init_file);

    std::string op;
    uint64_t key;
    int range;

    std::string insert("INSERT");
    std::string update("UPDATE");
    std::string read("READ");
    std::string scan("SCAN");
    int count = 0;
    while ((count < LOAD_SIZE) && infile_load.good()) {
        infile_load >> op >> key;
        if (op.compare(insert) != 0) {
            std::cout << "READING LOAD FILE FAIL!\n";
            return ;
        }
        init_keys.push_back(key);
        count++;
    }

    fprintf(stderr, "Loaded %d keys\n", count);

    std::ifstream infile_txn(txn_file);

    count = 0;
    while ((count < RUN_SIZE) && infile_txn.good()) {
        infile_txn >> op >> key;
        if (op.compare(insert) == 0) {
            ops.push_back(OP_INSERT);
            keys.push_back(key);
            ranges.push_back(1);
        } else if (op.compare(update) == 0) {
            ops.push_back(OP_UPDATE);
            keys.push_back(key);
            ranges.push_back(1);
        } else if (op.compare(read) == 0) {
            ops.push_back(OP_READ);
            keys.push_back(key);
            ranges.push_back(1);
        } else if (op.compare(scan) == 0) {
            infile_txn >> range;
            ops.push_back(OP_SCAN);
            keys.push_back(key);
            ranges.push_back(range);
        } else {
            std::cout << "UNRECOGNIZED CMD!\n";
            return;
        }
        count++;
    }

    std::atomic<int> range_complete, range_incomplete;
    range_complete.store(0);
    range_incomplete.store(0);

    if (index_type == TYPE_ART) {
        PACTree *pt = new PACTree();
        {
            // Load
            auto starttime = std::chrono::system_clock::now();
            tbb::parallel_for(tbb::blocked_range<uint64_t>(0, LOAD_SIZE), [&](const tbb::blocked_range<uint64_t> &scope) {
                pt->registerThread();
                for (uint64_t i = scope.begin(); i != scope.end(); i++) {
                    Key_t key(init_keys[i]);
                    Key k;
                    k.Set(key.getData(), key.keyLength);
                    pt->Insert(key, init_keys[i]);
                }
                pt->unregisterThread();

            });
            fprintf(stderr, "成功插入\n");
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now() - starttime);
            printf("Throughput: load, %f ,ops/us\n", (LOAD_SIZE * 1.0) / duration.count());
        }

        {
            // Run
            auto starttime = std::chrono::system_clock::now();
            tbb::parallel_for(tbb::blocked_range<uint64_t>(0, RUN_SIZE), [&](const tbb::blocked_range<uint64_t> &scope) {
                pt->registerThread();
                for (uint64_t i = scope.begin(); i != scope.end(); i++) {
                    if (ops[i] == OP_INSERT) {
                        Key_t key(keys[i]);
                        pt->Insert(key, keys[i]);
                    } else if (ops[i] == OP_READ) {
                        bool found;
                        Key_t key(keys[i]);
                        auto val = pt->lookup(key, &found);
                        if (val != keys[i]) {
                            std::cout << "[ART] wrong key read: " << val << " expected:" << keys[i] << std::endl;
                            exit(1);
                        }
                    } else if (ops[i] == OP_SCAN) {
                        LookupSnapshot snapshot{};
                        std::vector<std::pair<Key_t, Val_t>> result;
                        /* case 1: 按照顺序访问都能访问到 */
                        Key_t start(keys[i]);
                        Key_t end(keys[i + ranges[i]]);
                        size_t resultsSize = ranges[i];
                        pt->scan(start, end, resultsSize, snapshot, false, result);
                    } else if (ops[i] == OP_UPDATE) {
                        std::cout << "NOT SUPPORTED CMD!\n";
                        exit(0);
                    }
                }
                pt->unregisterThread();
            });
            fprintf(stderr, "成功执行操作\n");
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now() - starttime);
            printf("Throughput: run, %f ,ops/us\n", (RUN_SIZE * 1.0) / duration.count());
        }
        delete pt;
    }
}
#endif


int main(int argc, char **argv) {
    const std::string space_dir = "/mnt/pmem0/bench;/mnt/pmem1/bench";
    g_dir_config = std::make_shared<NVMDB::DirectoryConfig>(space_dir, true);
    InitGlobalThreadStorageMgr();
    InitThreadLocalStorage();
    // 初始化 Google Logging
    google::InitGoogleLogging(argv[0]);
    // 可选：设置日志输出文件或级别
    FLAGS_logtostderr = true;  // 将日志输出到标准错误流
    FLAGS_minloglevel = 0;     // INFO 日志及以上的日志级别
    if (argc != 6) {
        std::cout << "Usage: ./ycsb [index type] [ycsb workload type] [key distribution] [access pattern] [number of threads]\n";
        std::cout << "1. index type: art hot bwtree masstree clht\n";
        std::cout << "               fastfair levelhash cceh woart\n";
        std::cout << "2. ycsb workload type: a, b, c, e\n";
        std::cout << "3. key distribution: randint, string\n";
        std::cout << "4. access pattern: uniform, zipfian\n";
        std::cout << "5. number of threads (integer)\n";
        return 1;
    }

    printf("%s, workload%s, %s, %s, threads %s\n", argv[1], argv[2], argv[3], argv[4], argv[5]);

    int index_type;
    if (strcmp(argv[1], "art") == 0)
        index_type = TYPE_ART;
    else if (strcmp(argv[1], "hot") == 0) {
#ifdef HOT
        index_type = TYPE_HOT;
#else
        return 1;
#endif
    } else if (strcmp(argv[1], "bwtree") == 0)
        index_type = TYPE_BWTREE;
    else if (strcmp(argv[1], "masstree") == 0)
        index_type = TYPE_MASSTREE;
    else if (strcmp(argv[1], "clht") == 0)
        index_type = TYPE_CLHT;
    else if (strcmp(argv[1], "fastfair") == 0)
        index_type = TYPE_FASTFAIR;
    else if (strcmp(argv[1], "levelhash") == 0)
        index_type = TYPE_LEVELHASH;
    else if (strcmp(argv[1], "cceh") == 0)
        index_type = TYPE_CCEH;
    else if (strcmp(argv[1], "woart") == 0)
        index_type = TYPE_WOART;
    else {
        fprintf(stderr, "Unknown index type: %s\n", argv[1]);
        exit(1);
    }

    int wl;
    if (strcmp(argv[2], "a") == 0) {
        wl = WORKLOAD_A;
    } else if (strcmp(argv[2], "b") == 0) {
        wl = WORKLOAD_B;
    } else if (strcmp(argv[2], "c") == 0) {
        wl = WORKLOAD_C;
    } else if (strcmp(argv[2], "d") == 0) {
        wl = WORKLOAD_D;
    } else if (strcmp(argv[2], "e") == 0) {
        wl = WORKLOAD_E;
    } else {
        fprintf(stderr, "Unknown workload: %s\n", argv[2]);
        exit(1);
    }

    int kt;
    if (strcmp(argv[3], "randint") == 0) {
        kt = RANDINT_KEY;
    } else if (strcmp(argv[3], "string") == 0) {
        kt = STRING_KEY;
    } else {
        fprintf(stderr, "Unknown key type: %s\n", argv[3]);
        exit(1);
    }

    int ap;
    if (strcmp(argv[4], "uniform") == 0) {
        ap = UNIFORM;
    } else if (strcmp(argv[4], "zipfian") == 0) {
        ap = ZIPFIAN;
    } else {
        fprintf(stderr, "Unknown access pattern: %s\n", argv[4]);
        exit(1);
    }

    int num_thread = atoi(argv[5]);
    tbb::global_control global_limit(tbb::global_control::max_allowed_parallelism, num_thread);
#ifdef STRINGKEY
    std::vector<string> init_keys;
        std::vector<string> keys;
        std::vector<uint64_t> init_vals;
        std::vector<uint64_t> vals;
        std::vector<int> ranges;
        std::vector<int> ops;

        init_keys.reserve(LOAD_SIZE);
        init_vals.reserve(LOAD_SIZE);
        keys.reserve(RUN_SIZE);
        vals.reserve(RUN_SIZE);
        ranges.reserve(RUN_SIZE);
        ops.reserve(RUN_SIZE);

        memset(&init_keys[0], 0x00, LOAD_SIZE * sizeof(string));
        memset(&init_vals[0], 0x00, LOAD_SIZE * sizeof(uint64_t));
        memset(&keys[0], 0x00, RUN_SIZE * sizeof(string));
        memset(&vals[0], 0x00, RUN_SIZE * sizeof(uint64_t));
        memset(&ranges[0], 0x00, RUN_SIZE * sizeof(int));
        memset(&ops[0], 0x00, RUN_SIZE * sizeof(int));

        ycsb_load_run_string(index_type, wl, kt, ap, num_thread, init_keys, keys, init_vals,  vals, ranges, ops);
#else
        std::vector<uint64_t> init_keys;
        std::vector<uint64_t> keys;
        std::vector<int> ranges;
        std::vector<int> ops;

        init_keys.reserve(LOAD_SIZE);
        keys.reserve(RUN_SIZE);
        ranges.reserve(RUN_SIZE);
        ops.reserve(RUN_SIZE);

        memset(&init_keys[0], 0x00, LOAD_SIZE * sizeof(uint64_t));
        memset(&keys[0], 0x00, RUN_SIZE * sizeof(uint64_t));
        memset(&ranges[0], 0x00, RUN_SIZE * sizeof(int));
        memset(&ops[0], 0x00, RUN_SIZE * sizeof(int));

        ycsb_load_run_randint(index_type, wl, kt, ap, num_thread, init_keys, keys, ranges, ops);
#endif
    // 关闭 Google Logging
    DestroyThreadLocalStorage();
    g_dir_config.reset(); // 确保在主程序退出前销毁
    google::ShutdownGoogleLogging();
    return 0;
}
