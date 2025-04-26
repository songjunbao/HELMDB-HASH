//
// Created by song on 25-1-12.
//
#include "common/pactree/search_layer.h"

HashTable hashAdjTable;
bool end;
int hash_count = 0;
void PDLARTIndex::hash_workerThreadExec()
{
    while (true) {
        if (!hashops_queue.empty()) {
            hash_ApplyOperation();
        } else {
            if (!hashops.empty()) {
                // 将 hashops 推入队列
                std::vector<HashOps> *hashops_ptr = &hashops;  // 获取 hashops 的指针
                if (!hashops_queue.push(hashops_ptr)) {
                    std::cerr << "队列已满，无法推入数据！" << std::endl;
                    std::cerr << "队列已满，正在重试..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                // 重置 hashops 以便继续使用
                hashops.clear();  // 清空当前的 hashops 数据
                hash_ApplyOperation();
            } else {
                if (!end) {  // 连续检测超过 10 次为空时退出
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                break;
            }
        }
    }
}
void PDLARTIndex::hash_ApplyOperation()
{
    std::vector<HashOps> *hash_ops = nullptr;
    if (hashops_queue.pop(hash_ops)) {
        for (auto opsPtr : *hash_ops) {
            HashOps &ops = *opsPtr;
            hash_Insert(ops.prefix, ops.pTid, ops.version, ops.parent_ptr);
        }
    }
    // 在处理完后释放内存
}

TID PDLARTIndex::hashLookup(const Key &k)
{
    int len = 4;
    while (len <= 8) {
        auto prefix = k.extractPrefix(len);
        ValueEntry *vbuf;
        auto callback = [&](HashTable::RecordType record) { vbuf = std::move(record.value()); };
        if (hashAdjTable.Find(prefix, callback)) {
            auto res_node = vbuf->ptid->getVaddr();
            if (ART_ROWEX::N::IsLeaf(res_node)) {
                auto res2 = vbuf->parent_ptid->getVaddr()->version.load();
                auto res = vbuf->version;
                if (res != res2) {
                    fprintf(stderr, "failed version：%d vs. %d\n", res, res2);
                }
                lookcount++;
                if (lookcount % 1000000 == 0) {
                    fprintf(stderr, "查询成功hash：%d keys\n", lookcount);
                }
                return ART_ROWEX::N::GetLeaf(res_node);
            } else {
                len += 2;
            }
        } else {
            break;
        }
    }
    return 0;
}

bool PDLARTIndex::hash_Insert(uint64_t prefix, NVMPtr<ART_ROWEX::N> *pTid, uint64_t version,
                              NVMPtr<ART_ROWEX::N> *par_pTid)
{
    hash_count++;
    if (hash_count % 100000 == 0) {
        fprintf(stderr, "hash_count： %d keys\n", hash_count);
    }
    auto thread_info = hashAdjTable.getThreadInfo();
    //    if(hash_count % 10000 == 0){
    //        fprintf(stderr, "hash插入的键值数量： %d keys\n", hash_count);
    //        fprintf(stderr, "平均长度： %d keys\n", hash_average_len);
    //    }
    ValueEntry *vbuf;
    auto callback = [&](HashTable::RecordType record) { vbuf = std::move(record.value()); };
    if (hashAdjTable.Find(prefix, callback)) {
        vbuf->ptid = pTid;
        vbuf->version = version;
        vbuf->parent_ptid = par_pTid;
        //        vbuf->version = par_pTid->getVaddr()->version.load();
    } else {
        // 使用智能指针管理 adjHash 的生命周期
        // 将前5个字符的哈希值插入到布隆过滤器中
        //        filter.insert(hashKeyPart);  // 使用布隆过滤器插入前5个字符的前缀（哈希值）
        auto value = std::make_unique<ValueEntry>();
        value->version = version;
        value->ptid = pTid;
        value->parent_ptid = par_pTid;
        //        value->version = par_pTid->getVaddr()->version.load();
        hashAdjTable.Put(prefix, value.release(), thread_info);
    }
    return true;
}
