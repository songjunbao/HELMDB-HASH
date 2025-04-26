#ifndef ART_ROWEX_TREE_H
#define ART_ROWEX_TREE_H
#include <iostream>
#include "common/pdl_art/n.h"
#include <unordered_map>
#include <mutex>
#include "turbo/turbo_hash.h"
#include "bloom_filter.hpp"
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <boost/lockfree/queue.hpp>
using namespace turbo;
using namespace ART;

class HashOps {
public:
    uint64_t prefix;
    uint64_t version;
    NVMPtr<ART_ROWEX::N> *pTid;
    NVMPtr<ART_ROWEX::N> *parent_ptr;
    // 带参数的构造函数，可以在创建对象时直接初始化
    HashOps(uint64_t pre, uint64_t ver, NVMPtr<ART_ROWEX::N> *tid, NVMPtr<ART_ROWEX::N> *parent_tid)
        : prefix(pre), version(ver), pTid(tid), parent_ptr(parent_tid)
    {}
    // 定义解引用操作符
    HashOps &operator*()
    {
        return *this;  // 返回对象本身
    }
};
//// 声明一个全局变量，存储 HashOps 指针
// extern tbb::concurrent_vector<std::unique_ptr<HashOps>> hashops;
//  声明线程局部变量
extern thread_local std::vector<HashOps> hashops;
// 定义无锁队列，存储 std::vector<std::unique_ptr<HashOps>>
extern boost::lockfree::queue<std::vector<HashOps> *> hashops_queue;

namespace ART_ROWEX {
class Tree {
public:
    static constexpr uint64_t removeConditionCount = 2;
    static constexpr uint64_t defaultGenId = 3;
    using LoadKeyFunction = void (*)(TID tid, Key &key);
    uint64_t genId{0};
    LoadKeyFunction loadKey{nullptr};

    void Copy(TID *result, std::size_t &resultsFound, std::size_t &resultSize, TID &toContinue, N *node) const;
    void CopyReverse(TID *result, std::size_t &resultsFound, std::size_t &resultSize, TID &toContinue, N *node) const;
    bool FindStart(TID *result, std::size_t &resultsFound, std::size_t &resultSize, const Key &start, TID &toContinue,
                   N *node, N *parentNode, uint32_t level, uint32_t parentLevel) const;

    enum class CheckPrefixResult : uint8_t { MATCH, NO_MATCH, OPTIMISTIC_MATCH };

    enum class CheckPrefixPessimisticResult : uint8_t { MATCH, NO_MATCH, SKIPPED_LEVEL };

    enum class PCCompareResults : uint8_t { SMALLER, EQUAL, BIGGER, SKIPPED_LEVEL };
    enum class PCEqualsResults : uint8_t { PARTIAL_MATCH, BOTH_MATCH, CONTAINED, NO_MATCH, SKIPPED_LEVEL };
    static CheckPrefixResult CheckPrefix(N *n, const Key &k, uint32_t &level);

    static CheckPrefixPessimisticResult CheckPrefixPessimistic(N *n, const Key &k, uint32_t &level,
                                                               uint8_t &nonMatchingKey, Prefix &nonMatchingPrefix,
                                                               LoadKeyFunction loadKey);

    static PCCompareResults CheckPrefixCompare(const N *n, const Key &k, uint32_t &level, LoadKeyFunction loadKey);

    static PCEqualsResults CheckPrefixEquals(const N *n, uint32_t &level, const Key &start, const Key &end,
                                             LoadKeyFunction loadKey);

    void Recover();
    explicit Tree(LoadKeyFunction loadKey);

    Tree(const Tree &) = delete;

    Tree &operator=(const Tree &) = delete;

    Tree(Tree &&t) noexcept : root(t.root), loadKey(t.loadKey) {}

    Tree &operator=(Tree &&t) noexcept {
        root = t.root;
        loadKey = t.loadKey;
        return *this;
    }

    ~Tree();

    ART::ThreadInfo GetThreadInfo();

    TID Lookup(const Key &k, ART::ThreadInfo &threadEpochInfo) const;
    void hashLookup(const Key &k, std::size_t &resultsFound, TID *result);
    TID LookupNext(const Key &k, ART::ThreadInfo &threadEpochInfo);
    void insertIntoHashAdjTable(const Key &k, NVMPtr<N> &pTid, uint32_t level, NVMPtr<N> &newNodePtr,
                                uint32_t start_level);
    NVMPtr<N> LookupPtr(const Key &k, uint32_t level, uint64_t prefix);
    void Insert(const Key &k, TID tid, ART::ThreadInfo &epochInfo);
    void Remove(const Key &k, TID tid, ART::ThreadInfo &epochInfo);

private:
    NVMPtr<N> root;
    TID CheckKey(TID tid, const Key &k) const;
    int hash_average_len = 0;
    int hash_count = 0;         // 插入hash表的数量
    int art_average_level = 0;  // 平均leaf节点的层级
    int key_count = 0;          // 总的key数量
    int sum_level = 0;          // 叶节点的总层级
    int res_count = 0;          // hash查询到结果的数量
    int miss_count = 0;
    // 初始化布隆过滤器
    bloom_parameters parameters;
    bloom_filter filter;
    ART::Epoch epoch{256};
    OpStruct oplogs[10000];
    uint64_t oplogsCount{0};
};

}  // namespace ART_ROWEX
#endif  // ART_ROWEX_TREE_H
