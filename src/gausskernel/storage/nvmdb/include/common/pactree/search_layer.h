#ifndef SEARCHLAYER_H
#define SEARCHLAYER_H

#include "common/pdl_art/tree.h"
#include <vector>
#include <memory>
#include <iostream>
#include "turbo/turbo_hash.h"
#include <thread>
#include <atomic>  // 引入 atomic 头文件

constexpr int DEFAULT_INSERT_NUM = 2;


class ValueEntry {
public:
    uint32_t level;
    NVMPtr<ART_ROWEX::N> *ptid;//存储内部节点
    NVMPtr<ART_ROWEX::N> *parent_ptid;//存储内部节点
    uint64_t version;
};

using HashTable = turbo::unordered_map<size_t, ValueEntry*>;
extern HashTable hashAdjTable;
extern bool end;  // 确保 atomic 类型的正确初始化;
extern int hash_count;
inline void LoadIntKeyFunction(TID tid, Key &k) {
    k.SetInt(*reinterpret_cast<uint64_t *>(tid));
}

inline void LoadStringKeyFunction(TID tid, Key &k) {
    auto pkey = reinterpret_cast<Key_t *>(tid);
    k.Set(pkey->getData(), pkey->keyLength);
}



class PDLARTIndex {
public:
    std::thread* workerThread = new std::thread(&PDLARTIndex::hash_workerThreadExec, this);; // 保存线程对象
    int group{0};
    uint8_t grpMask{0};
    PDLARTIndex() {
        PMEMoid oid;
        PMem::alloc(sizeof(ART_ROWEX::Tree), (void **)&idxPtr, &oid);
        idx = new (idxPtr.getVaddr()) ART_ROWEX::Tree(LoadStringKeyFunction);
        dummyIdx = new ART_ROWEX::Tree(LoadIntKeyFunction);

    }

    ~PDLARTIndex() {
        delete dummyIdx;
    }

    void Init() {
        idx = (ART_ROWEX::Tree *)(idxPtr.getVaddr());
        idx->genId++;
        idx->loadKey = LoadStringKeyFunction;
        dummyIdx = new ART_ROWEX::Tree(LoadIntKeyFunction);

    }

    void SetGroupId(int nma) {
        this->group = nma;
        this->grpMask = 1 << nma;
    }

    void SetKey(Key &k, uint64_t key) {
        k.SetInt(key);
    }

    void SetKey(Key &k, StringKey<KEYLENGTH> key) {
        k.Set(key.getData(), key.keyLength);
    }

    bool Insert(Key_t key, void *ptr) {
        auto t = dummyIdx->GetThreadInfo();
        Key k;
        SetKey(k, key);
        idx->Insert(k, reinterpret_cast<unsigned long>(ptr), t);
        // 判断 hashops 的容量是否大于 1000
        if (hashops.size() >= 1000) {
            // 将 hashops 推入队列
            std::vector<HashOps>* hashops_ptr = &hashops;  // 获取 hashops 的指针
            if (!hashops_queue.push(hashops_ptr)) {
                std::cerr << "队列已满，无法推入数据！" << std::endl;
            }
            // 重置 hashops 以便继续使用
            hashops.clear();  // 清空当前的 hashops 数据
        }
        if (key < curMin)
            curMin = key;
        numInserts++;
        return true;
    }
    void hash_workerThreadExec();
    void hash_ApplyOperation();
    bool hash_Insert(uint64_t prefix, NVMPtr<ART_ROWEX::N>* pTid, uint64_t version, NVMPtr<ART_ROWEX::N>* par_pTid);
    TID hashLookup(const Key &k);
    bool remove(Key_t key, void *ptr) {
        auto t = dummyIdx->GetThreadInfo();
        Key k;
        SetKey(k, key);
        idx->Remove(k, reinterpret_cast<unsigned long>(ptr), t);
        numInserts--;
        return true;
    }

    // Gets the value of the key if present or the value of key just less than/greater than key
    void *lookup(Key_t &key) {
        auto t = dummyIdx->GetThreadInfo();
        Key endKey;
        SetKey(endKey, key);
        TID result = 0;
        if(hash_count > 300000){
            result =  hashLookup(endKey);
        }
//        if(result == 0) {
            auto result2 = idx->LookupNext(endKey, t);
//        }
//            if (result != result2 && result != 0) {
//                fprintf(stderr, "failed hash：%d vs. %d\n", result, result2);
//            }
        return reinterpret_cast<void *>(result2);
    }

    void *lookup2(Key_t key) {
        if (key <= curMin) {
            return nullptr;
        }
        auto t = dummyIdx->GetThreadInfo();
        Key endKey;
        SetKey(endKey, key);

        auto result = idx->Lookup(endKey, t);
        return reinterpret_cast<void *>(result);
    }

    // Art segfaults if range operation is done when there are less than 2 keys
    bool IsEmpty() {
        return (numInserts < DEFAULT_INSERT_NUM);
    }

    uint32_t Size() {
        return numInserts;
    }
private:
    int lookcount = 0;
    Key minKey;
    Key_t curMin;
    NVMPtr<ART_ROWEX::Tree> idxPtr;
    ART_ROWEX::Tree *idx{nullptr};
    ART_ROWEX::Tree *dummyIdx{nullptr};
    uint32_t numInserts{0};
};

using SearchLayer = PDLARTIndex;

#endif
