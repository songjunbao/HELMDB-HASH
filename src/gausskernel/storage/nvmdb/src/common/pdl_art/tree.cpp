#include "common/pdl_art/tree.h"
#include <chrono>
#include <shared_mutex>
// 线程局部变量，每个线程都有独立的实例
thread_local std::vector<HashOps> hashops;
boost::lockfree::queue<std::vector<HashOps> *> hashops_queue(1024);
namespace ART_ROWEX {
void Tree::Recover() {
    for (int i = 0; i < oplogsCount; i++) {
        if (oplogs[i].op == OpStruct::insert) {
            pmemobj_free(&oplogs[i].newNodeOid);
        }
    }
}
Tree::Tree(LoadKeyFunction loadKey) : loadKey(loadKey) {
    NVMPtr<OpStruct> ologPtr;
    PMEMoid oid;
    PMem::alloc(sizeof(OpStruct), (void **)&ologPtr, &oid);
    OpStruct *olog = ologPtr.getVaddr();
    NVMPtr<N> nRootPtr;
    PMem::alloc(sizeof(N256), (void **)&nRootPtr, &(olog->newNodeOid));
    oplogsCount = 1;
    flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
    smp_wmb();

    genId = defaultGenId;
    N256 *rootRawPtr = (N256 *)new (nRootPtr.getVaddr()) N256(0, {});

    flushToNVM(reinterpret_cast<char *>(rootRawPtr), sizeof(N256));
    smp_wmb();
    this->loadKey = loadKey;
    root = nRootPtr;
    flushToNVM(reinterpret_cast<char *>(this), sizeof(Tree));
    smp_wmb();
    // How many elements roughly do we expect to insert?
    parameters.projected_element_count = 50000;
    // Maximum tolerable false positive probability? (0,1)
    parameters.false_positive_probability = 0.01; // 1 in 100
    // Simple randomizer (optional)
    parameters.random_seed = 0xA5A5A5A5;
    if (!parameters)
    {
        std::cout << "Error - Invalid set of bloom filter parameters!" << std::endl;
    }
    parameters.compute_optimal_parameters();
    //Instantiate Bloom Filter
    filter = bloom_filter(parameters);
}

Tree::~Tree(){
//    hashAdjTable.ReleaseRecords();  // 清空容器
}

ART::ThreadInfo Tree::GetThreadInfo() {
    return ART::ThreadInfo(this->epoch);
}

TID Tree::Lookup(const Key &k, ART::ThreadInfo &threadEpochInfo) const {
    ART::EpochGuardReadonly epochGuard(threadEpochInfo);
    NVMPtr<N> nodePtr = root;
    N *node = (N *)nodePtr.getVaddr();
    uint32_t level = 0;
    bool optimisticPrefixMatch = false;
    while (true) {
        switch (CheckPrefix(node, k, level)) {  // increases level
            case CheckPrefixResult::NO_MATCH:
                return 0;
            case CheckPrefixResult::OPTIMISTIC_MATCH:
                optimisticPrefixMatch = true;
                // fallthrough
            case CheckPrefixResult::MATCH: {
                if (k.GetKeyLen() <= level) {
                    return 0;
                }
                node = N::GetChild(k[level], node);
                if (node == nullptr) {
                    return 0;
                }
                if (N::IsLeaf(node)) {
                    TID tid = N::GetLeaf(node);
                    if (level < k.GetKeyLen() - 1 || optimisticPrefixMatch) {
                        return CheckKey(tid, k);
                    } else {
                        return tid;
                    }
                }
            }
        }
        level++;
    }
}

TID Tree::CheckKey(const TID tid, const Key &k) const {
    Key kt;
    this->loadKey(tid, kt);
    if (k == kt) {
        return tid;
    }
    return 0;
}
//// 插入哈希邻接表函数：分段插入哈希邻接表
//void Tree::insertIntoHashAdjTable(const Key &k, NVMPtr<N>& pTid, uint32_t level, NVMPtr<N>& newNodePtr, uint32_t start_level) {
//    auto thread_info = hashAdjTable.getThreadInfo ();
////    if(hash_count % 10000 == 0){
////        fprintf(stderr, "hash插入的键值数量： %d keys\n", hash_count);
////        fprintf(stderr, "平均长度： %d keys\n", hash_average_len);
////    }
//    ValueEntry* vbuf;
//    auto callback = [&] (HashTable::RecordType record) {vbuf = std::move(record.value());};
//    uint32_t prefix_count = level + (level % 2);
//    auto hashKeyPart = k.extractPrefix(prefix_count);  // 提取前4个字符的指针和长度
//    if (hashAdjTable.Find(hashKeyPart,callback)) {
//        vbuf->ptid = pTid;
//        vbuf->level = prefix_count;
//    }else{
//        // 使用智能指针管理 adjHash 的生命周期
//        // 将前5个字符的哈希值插入到布隆过滤器中
//        //        filter.insert(hashKeyPart);  // 使用布隆过滤器插入前5个字符的前缀（哈希值）
//        auto value = std::make_unique<ValueEntry>();
//        value->level = prefix_count;
//        value->ptid = pTid;
//        hashAdjTable.Put (hashKeyPart, value.release(), thread_info);
//    }
//}

//NVMPtr<N> Tree::LookupPtr(const Key &k, uint32_t level,size_t prefix){
//    // 使用读锁保护 hashAdjTable 的查找
//    //std::shared_lock<std::shared_mutex> readLock(hashAdjTableMutex);
//    NVMPtr<N> res;
//    HashAdjEntry* vbuf = nullptr;
//    auto callback = [&] (HashTable::RecordType record) {
//        vbuf = record.value();
//    };
//    hashAdjTable.Find(prefix,  callback);
//    if (vbuf != nullptr) {
//        auto remaining = k.extractRemaining(level);
//        auto res_callback = [&](turbo::unordered_map<size_t, NVMPtr<N> *>::RecordType record) {
//            res = *record.value();
//        };
//        vbuf->adjMap.Find(remaining, res_callback);
//    }
//    return res;
//}

void Tree::Insert(const Key &k, TID tid, ART::ThreadInfo &epochInfo) {
    ART::EpochGuard epochGuard(epochInfo);
restart:
    bool needRestart = false;

    NVMPtr<N> nextNodePtr = root;

    N *node = nullptr;
    uint8_t nodeKey = 0;
    NVMPtr<N> nodePtr;
    N *nextNode = (N *)root.getVaddr();
    uint32_t level = 0;

    while (true) {
        N* parentNode = node;
        uint8_t parentKey = nodeKey;
        NVMPtr<N> parentPtr = nodePtr;
        node = nextNode;
        nodePtr = nextNodePtr;

        auto v = node->GetVersion();

        uint32_t nextLevel = level;

        uint8_t nonMatchingKey;
        Prefix remainingPrefix;
        switch (CheckPrefixPessimistic(node, k, nextLevel, nonMatchingKey, remainingPrefix, this->loadKey)) {  // increases level
            case CheckPrefixPessimisticResult::SKIPPED_LEVEL:
                goto restart;
            case CheckPrefixPessimisticResult::NO_MATCH: {
                 DCHECK(nextLevel < k.GetKeyLen());  // prevent duplicate key
                node->LockVersionOrRestart(v, needRestart, genId);
                if (needRestart) {
                    goto restart;
                }
                // 1) Create new node which will be parent of node, Set common prefix, level to this node
                Prefix prefix = node->GetPrefix();
                prefix.prefixCount = nextLevel - level;

                NVMPtr<N> newNodePtr;
                oplogs[oplogsCount].op = OpStruct::insert;
                oplogs[oplogsCount].oldNodePtr = (void *)parentPtr.getRawPtr();
                PMem::alloc(sizeof(N4), reinterpret_cast<void **>(&newNodePtr), &(oplogs[oplogsCount].newNodeOid));
                oplogsCount++;
                flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
                smp_wmb();

                N4 *newNode = (N4 *)new (newNodePtr.getVaddr()) N4(nextLevel, prefix);
                //tid:事务id,ptid根据事务id获取,用于指向叶子节点
                NVMPtr<N> pTid(0, (unsigned long)N::SetLeaf(tid));
                // 2)  add node and (tid, *k) as children
                //这里的insert,ptid指向的就是该key的叶节点，newNode是一个内部节点
                newNode->Insert(k[nextLevel], pTid, false);
                //这里的insert,nodePtr指向的是未匹配的其他内部节点，也就是原来获取的node地址
                newNode->Insert(nonMatchingKey, nodePtr, false);
                flushToNVM(reinterpret_cast<char *>(static_cast<void *>(newNode)), sizeof(N4));
                smp_wmb();

                // 3) lockVersionOrRestart, update parentNode to point to the new node, unlock
                CHECK(parentNode != nullptr);
                parentNode->WriteLockOrRestart(needRestart, genId);
                if (needRestart) {
                    PMem::free((void *)newNodePtr.getRawPtr());
                    node->WriteUnlock();
                    goto restart;
                }
                N::Change(parentNode, parentKey, newNodePtr);
                oplogs[oplogsCount].op = OpStruct::done;
                auto version = newNode->version.load();
                parentNode->WriteUnlock();
                // 4) update prefix of node, unlock
                node->SetPrefix(remainingPrefix.prefix, node->GetPrefix().prefixCount - ((nextLevel - level) + 1), true);
                oplogsCount = 0;
                flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
                smp_wmb();
                node->WriteUnlock();
                //这里在确保art内部结构调整完成持久化之后再去执行插入哈希邻接表操作，保证数据的一致性
                /* 插入哈希邻接表策略：
                 * 1）待插入level>5,则执行插入哈希邻接表操作,这里的level可以用nextLevel来判断,nextLevel正好是内部节点匹配的前缀的长度-1
                 * 2）考虑到前缀尽可能的复用，则每5个层级进行前缀分割操作，并将其插入邻接表
                 * 3）对于内部节点的索引，在邻接表存储的是子节点的指针，对于叶子节点的索引，则插入哈希邻接表存储的是ptid
                 * 4)为了更好的匹配ART的特性，hash索引的前缀不需要把key的所有字符都作为hash的key,只需要与ART匹配的前缀长度一致即可
                 * 5）为了更好的辨别是否索引到叶子节点，则需要判断索引的指针是否是指向叶子节点的指针，在NVMPtr<N>中加入一个标志位，
                 *    用于辨别是否索引到叶子节点
                 * 6) 为了更方便索引叶子节点，由于采用节点复用，目前打算采取的策略是如果ART到leaf的前缀不等于key,则邻接表的索引key按照5
                 *    长度进行补全，或者直接包含整个key
                 * 举例:
                 * （1）key:fnaskdlfjnfd
                 * fnaskdlf->leaf(8个前缀则索引到了leaf)
                 * 则在hashAdjTable中存储的key为fnask->dlfjn->ptid
                 *
                 * （2）key:fnaskdlfj
                 * fnaskdlf->leaf(8个前缀则索引到了leaf)
                 * 则在hashAdjTable中存储的key为fnask->dlfj->ptid
                 * 将其标志位设置为1，表示索引到了叶子节点
                 * */
                art_average_level = (art_average_level * key_count + nextLevel) / (++key_count);
                if(nextLevel > 2){
                    uint32_t prefix_count = nextLevel + (nextLevel % 2);
                    if(prefix_count != 4){
                        fprintf(stderr, "cur_length1: %d\n", prefix_count);
                    }
                    auto hashKeyPart = k.extractPrefix(prefix_count);
                    hashops.push_back(HashOps(hashKeyPart, version, &pTid, &newNodePtr));
                    //记录需要插入hash表键值的平均长度
                    hash_average_len = (hash_average_len * hash_count + nextLevel) / (++hash_count);
                }
                return;
            }
            case CheckPrefixPessimisticResult::MATCH:
                break;
        }
        level = nextLevel;
        nodeKey = k[level];
        nextNodePtr = N::GetChildNVMPtr(nodeKey, node);
        nextNode = nextNodePtr.getVaddr();
        if (nextNode == nullptr) {
            node->LockVersionOrRestart(v, needRestart, genId);
            if (needRestart) {
                goto restart;
            }

            NVMPtr<N> pTid(0, (unsigned long)N::SetLeaf(tid));

            N::InsertAndUnlock(node, parentNode, parentKey, nodeKey, pTid, epochInfo, needRestart,
                               &oplogs[oplogsCount], genId);
            oplogsCount = 0;
            flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
            smp_wmb();
            if (needRestart){
                goto restart;
            }
            art_average_level = (art_average_level * key_count + level) / (++key_count);
            if(level > 2){
                uint32_t prefix_count = level + (level % 2);
//                if(prefix_count != 4){
//                    fprintf(stderr, "cur_length2: %d\n", prefix_count);
//                }
                auto hashKeyPart = k.extractPrefix(prefix_count);
                auto verison  = node->version.load();
                hashops.push_back(HashOps(hashKeyPart, verison, &pTid, &nodePtr));
                //记录需要插入hash表键值的平均长度
                hash_average_len = (hash_average_len * hash_count + level) / (++hash_count);
            }
            return;
        }
        if (N::IsLeaf(nextNode)) {
            node->LockVersionOrRestart(v, needRestart, genId);
            if (needRestart) {
                goto restart;
            }

            Key key;
            loadKey(N::GetLeaf(nextNode), key);
            if (key == k) {
                NVMPtr<N> pTid(0, (unsigned long)N::SetLeaf(tid));
                //需要更新ptid
                N::Change(node, k[level], pTid);
                oplogsCount = 0;
                flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
                smp_wmb();
                auto verison  = node->version.load();
                node->WriteUnlock();
                if(level > 2){
                    //记录需要插入hash表键值的平均长度
                    hash_average_len = (hash_average_len * hash_count + level) / (++hash_count);
                    uint32_t prefix_count = level + (level % 2);
//                    if(prefix_count != 4){
//                        fprintf(stderr, "cur_length3: %d\n", prefix_count);
//                    }
                    auto hashKeyPart = k.extractPrefix(prefix_count);
                    hashops.push_back(HashOps(hashKeyPart, verison, &pTid, &nodePtr));
                }
                return;
            }

            level++;
//            DCHECK(level < key.GetKeyLen());  // prevent inserting when prefix of key exists already

            uint32_t prefixLength = 0;
            while (key[level + prefixLength] == k[level + prefixLength]) {
                prefixLength++;
            }

            NVMPtr<N> n4Ptr;
            oplogs[oplogsCount].op = OpStruct::insert;
            oplogs[oplogsCount].oldNodePtr = (void *)nodePtr.getRawPtr();
            PMem::alloc(sizeof(N4), (void **)&n4Ptr, &(oplogs[oplogsCount].newNodeOid));
            oplogsCount++;
            flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
            smp_wmb();

            N4 *n4 = (N4 *)new (n4Ptr.getVaddr()) N4(level + prefixLength, &k[level], prefixLength);
            NVMPtr<N> pTid(0, (unsigned long)N::SetLeaf(tid));

            n4->Insert(k[level + prefixLength], pTid, false);
            n4->Insert(key[level + prefixLength], nextNodePtr, false);
//            n4->version.fetch_add(1);
            node->version.fetch_add(1);
            auto version = node->version.load();
//            nextNodePtr.version.fetch_add(1);
            flushToNVM(reinterpret_cast<char *>(n4), sizeof(N4));
            smp_wmb();
            N::Change(node, k[level - 1], n4Ptr);
            oplogs[oplogsCount].op = OpStruct::done;

            oplogsCount = 0;
            flushToNVM(reinterpret_cast<char *>(&oplogsCount), sizeof(uint64_t));
            smp_wmb();
            node->WriteUnlock();
            uint32_t prefix_level = level + prefixLength;
            art_average_level = (art_average_level * key_count + prefix_level) / (++key_count);
            art_average_level = (art_average_level * key_count + prefixLength) / (key_count);
            if(prefix_level > 2){
                auto old_level = level - 1;
                uint32_t prefix_count = prefix_level + (prefix_level % 2);
                uint32_t old_prefix_count = old_level + (old_level % 2);
//                if(prefix_count != 4){
//                    fprintf(stderr, "cur_length4: %d\n", prefix_count);
//                }
                //原来该层级是叶节点，目前已经变成了内部节点，因此需要更新该层级的hash表
                while(old_prefix_count < prefix_count){
                    auto old_hashKeyPart = k.extractPrefix(old_prefix_count);
                    hashops.push_back(HashOps(old_hashKeyPart, version, &n4Ptr, &nodePtr));
                    old_prefix_count += 2;
                }
                auto hashKeyPart = k.extractPrefix(prefix_count);
                hashops.push_back(HashOps(hashKeyPart, 0, &pTid,&n4Ptr));
                hashKeyPart = key.extractPrefix(prefix_count);
                hashops.push_back(HashOps(hashKeyPart, 0, &nextNodePtr, &n4Ptr));
//                insertIntoHashAdjTable(k , pTid, prefix_level, n4Ptr, level);
//                insertIntoHashAdjTable(key , nextNodePtr, prefix_level, n4Ptr, level);
                //记录需要插入hash表键值的平均长度
                hash_average_len = (hash_average_len * hash_count + level) / (++hash_count);
            }
            return;
        }
        level++;
    }
}

void Tree::Remove(const Key &k, TID tid, ART::ThreadInfo &threadInfo) {
    ART::EpochGuard epochGuard(threadInfo);
restart:
    bool needRestart = false;

    N *node = nullptr;
    N *nextNode = root.getVaddr();
    uint8_t nodeKey = 0;
    uint32_t level = 0;
    NVMPtr<char> valuePtr;
    valuePtr.setRawPtr((void *)tid);

    while (true) {
        N *parentNode = node;
        uint8_t parentKey = nodeKey;
        node = nextNode;
        auto v = node->GetVersion();

        switch (CheckPrefix(node, k, level)) {
            case CheckPrefixResult::NO_MATCH:
                if (N::IsObsolete(v) || !node->ReadUnlockOrRestart(v)) {
                    goto restart;
                }
                return;
            case CheckPrefixResult::OPTIMISTIC_MATCH:
                // fallthrough
            case CheckPrefixResult::MATCH: {
                nodeKey = k[level];
                nextNode = N::GetChild(nodeKey, node);
                if (nextNode == nullptr) {
                    if (N::IsObsolete(v) || !node->ReadUnlockOrRestart(v)) {
                        goto restart;
                    }
                    return;
                }
                if (N::IsLeaf(nextNode)) {
                    node->LockVersionOrRestart(v, needRestart, genId);

                    if (needRestart) {
                        goto restart;
                    }
                    if (N::GetLeaf(nextNode) != (unsigned long)valuePtr.getVaddr()) {
                        node->WriteUnlock();
                        return;
                    }
                    DCHECK(parentNode == nullptr || node->GetCount() != 1);

                    if (node->GetCount() == removeConditionCount && node != (N *)(root.getVaddr())) {
                        // 1. check remaining entries
                        N *secondNodeN;
                        NVMPtr<N> secondNodePtr;
                        uint8_t secondNodeK;
                        std::tie(secondNodePtr, secondNodeK) = N::GetSecondChild(node, nodeKey);
                        secondNodeN = secondNodePtr.getVaddr();
                        CHECK(parentNode != nullptr);
                        if (N::IsLeaf(secondNodeN)) {
                            parentNode->WriteLockOrRestart(needRestart, genId);
                            if (needRestart) {
                                node->WriteUnlock();
                                goto restart;
                            }
                            N::Change(parentNode, parentKey, secondNodePtr);

                            parentNode->WriteUnlock();
                            node->WriteUnlockObsolete();
                            this->epoch.MarkNodeForDeletion(node, threadInfo);
                        } else {
                            uint64_t vChild = secondNodeN->GetVersion();
                            secondNodeN->LockVersionOrRestart(vChild, needRestart, genId);
                            if (needRestart) {
                                node->WriteUnlock();
                                goto restart;
                            }
                            parentNode->WriteLockOrRestart(needRestart, genId);
                            if (needRestart) {
                                node->WriteUnlock();
                                secondNodeN->WriteUnlock();
                                goto restart;
                            }

                            N::Change(parentNode, parentKey, secondNodePtr);
                            secondNodeN->AddPrefixBefore(node, secondNodeK);

                            parentNode->WriteUnlock();
                            node->WriteUnlockObsolete();
                            this->epoch.MarkNodeForDeletion(node, threadInfo);
                            secondNodeN->WriteUnlock();
                        }
                    } else {
                        N::RemoveAndUnlock(node, k[level], parentNode, parentKey, threadInfo, needRestart, &oplogs[0], genId);
                    }
                    if (needRestart) {
                        goto restart;
                    }
                    return;
                }
                level++;
            }
        }
    }
}

Tree::CheckPrefixResult Tree::CheckPrefix(N *n, const Key &k, uint32_t &level) {
    if (k.GetKeyLen() <= n->GetLevel()) {
        return CheckPrefixResult::NO_MATCH;
    }
    Prefix p = n->GetPrefix();
    if (p.prefixCount + level < n->GetLevel()) {
        level = n->GetLevel();
        return CheckPrefixResult::OPTIMISTIC_MATCH;
    }
    if (p.prefixCount > 0) {
        for (uint32_t i = ((level + p.prefixCount) - n->GetLevel());
             i < std::min(p.prefixCount, MAX_STORED_PREFIX_LENGTH); ++i) {
            if (p.prefix[i] != k[level]) {
                return CheckPrefixResult::NO_MATCH;
            }
            ++level;
        }
        if (p.prefixCount > MAX_STORED_PREFIX_LENGTH) {
            level += p.prefixCount - MAX_STORED_PREFIX_LENGTH;
            return CheckPrefixResult::OPTIMISTIC_MATCH;
        }
    }
    return CheckPrefixResult::MATCH;
}

Tree::CheckPrefixPessimisticResult Tree::CheckPrefixPessimistic(N *n,
                                                                const Key &k,
                                                                uint32_t &level,
                                                                uint8_t &nonMatchingKey,
                                                                Prefix &nonMatchingPrefix,
                                                                LoadKeyFunction loadKeyFunc) {
    Prefix p = n->GetPrefix();
    //是确保在多线程环境下，当某个节点的前缀跨越多个层级时，能够正确处理这些层级的变化，避免潜在的冲突和不一致问题。
    //level是当前层级，n->GetLevel()是节点所在的层级。
    if (p.prefixCount + level < n->GetLevel()) {
        return CheckPrefixPessimisticResult::SKIPPED_LEVEL;
    }
    if (p.prefixCount > 0) {
        uint32_t prevLevel = level;
        Key kt;
        for (uint32_t i = ((level + p.prefixCount) - n->GetLevel()); i < p.prefixCount; ++i) {
            if (i == MAX_STORED_PREFIX_LENGTH) {
                // loadKeyFunc的作用是加载更多的键值，因为一个节点的存储的前缀的长度可能大于MAX_STORED_PREFIX_LENGTH，需要加载多出的前缀值进行匹配
                loadKeyFunc(N::GetAnyChildTid(n), kt);
            }
            uint8_t curKey = i >= MAX_STORED_PREFIX_LENGTH ? kt[level] : p.prefix[i];
            if (curKey != k[level]) {
                nonMatchingKey = curKey;
                if (p.prefixCount > MAX_STORED_PREFIX_LENGTH) {
                    if (i < MAX_STORED_PREFIX_LENGTH) {
                        loadKeyFunc(N::GetAnyChildTid(n), kt);
                    }
                    for (uint32_t j = 0; j < std::min((p.prefixCount - (level - prevLevel) - 1),
                                                      static_cast<uint32_t>(MAX_STORED_PREFIX_LENGTH));
                         ++j) {
                        nonMatchingPrefix.prefix[j] = kt[level + j + 1];
                    }
                } else {
                    for (uint32_t j = 0; j < p.prefixCount - i - 1; ++j) {
                        nonMatchingPrefix.prefix[j] = p.prefix[i + j + 1];
                    }
                }
                return CheckPrefixPessimisticResult::NO_MATCH;
            }
             ++level;
        }
    }
    return CheckPrefixPessimisticResult::MATCH;
}

typename Tree::PCCompareResults Tree::CheckPrefixCompare(const N *n,
                                                         const Key &k,
                                                         uint32_t &level,
                                                         LoadKeyFunction loadKeyFunc) {
    Prefix p = n->GetPrefix();
    if (p.prefixCount + level < n->GetLevel()) {
        return PCCompareResults::SKIPPED_LEVEL;
    }
    if (p.prefixCount > 0) {
        Key kt;
        for (uint32_t i = ((level + p.prefixCount) - n->GetLevel()); i < p.prefixCount; ++i) {
            if (i == MAX_STORED_PREFIX_LENGTH) {
                loadKeyFunc(N::GetAnyChildTid(n), kt);
            }
            uint8_t kLevel = (k.GetKeyLen() > level) ? k[level] : 0;

            uint8_t curKey = i >= MAX_STORED_PREFIX_LENGTH ? kt[level] : p.prefix[i];
            if (curKey < kLevel) {
                return PCCompareResults::SMALLER;
            } else if (curKey > kLevel) {
                return PCCompareResults::BIGGER;
            }
            ++level;
        }
    }
    return PCCompareResults::EQUAL;
}

typename Tree::PCEqualsResults Tree::CheckPrefixEquals(const N *n,
                                                       uint32_t &level,
                                                       const Key &start,
                                                       const Key &end,
                                                       LoadKeyFunction loadKeyFunc) {
    Prefix p = n->GetPrefix();
    if (p.prefixCount + level < n->GetLevel()) {
        return PCEqualsResults::SKIPPED_LEVEL;
    }
    if (p.prefixCount > 0) {
        Key kt;
        for (uint32_t i = ((level + p.prefixCount) - n->GetLevel()); i < p.prefixCount; ++i) {
            if (i == MAX_STORED_PREFIX_LENGTH) {
                loadKeyFunc(N::GetAnyChildTid(n), kt);
            }
            uint8_t startLevel = (start.GetKeyLen() > level) ? start[level] : 0;
            uint8_t endLevel = (end.GetKeyLen() > level) ? end[level] : 0;

            uint8_t curKey = i >= MAX_STORED_PREFIX_LENGTH ? kt[level] : p.prefix[i];
            if (curKey > startLevel && curKey < endLevel) {
                return PCEqualsResults::CONTAINED;
            } else if (curKey < startLevel || curKey > endLevel) {
                return PCEqualsResults::NO_MATCH;
            }
            ++level;
        }
    }
    return PCEqualsResults::BOTH_MATCH;
}

void Tree::Copy(TID *result,
                std::size_t &resultsFound,
                std::size_t &resultSize,
                TID &toContinue,
                N *node) const {
    if (N::IsLeaf(node)) {
        if (resultsFound == resultSize) {
            toContinue = N::GetLeaf(node);
            return;
        }
        result[resultsFound] = N::GetLeaf(node);
        resultsFound++;
    } else {
        N *child = N::GetSmallestChild(node, 0);
        Copy(result, resultsFound, resultSize, toContinue, child);
    }
}

void Tree::CopyReverse(TID *result,
                       std::size_t &resultsFound,
                       std::size_t &resultSize,
                       TID &toContinue,
                       N *node) const {
    if (N::IsLeaf(node)) {
        if (resultsFound == resultSize) {
            toContinue = N::GetLeaf(node);
            return;
        }
        result[resultsFound] = N::GetLeaf(node);
        resultsFound++;
    } else {
        N *child = N::GetLargestChild(node, 255);
        CopyReverse(result, resultsFound, resultSize, toContinue, child);
    }
}

bool Tree::FindStart(TID *result,
                     std::size_t &resultsFound,
                     std::size_t &resultSize,
                     const Key &start,
                     TID &toContinue,
                     N *node, N *parentNode,
                     uint32_t level,
                     uint32_t parentLevel) const {
    if (N::IsLeaf(node)) {
        Copy(result, resultsFound, resultSize, toContinue, node);
        return false;
    }

    PCCompareResults prefixResult = CheckPrefixCompare(node, start, level, loadKey);
    switch (prefixResult) {
        case PCCompareResults::BIGGER: {
            N *childNode = nullptr;
            if (start[parentLevel] != 0) {
                childNode = N::GetLargestChild(parentNode, start[parentLevel] - 1);
            }
            if (childNode != nullptr) {
                CopyReverse(result, resultsFound, resultSize, toContinue, childNode);
            } else {
                Copy(result, resultsFound, resultSize, toContinue, node);
            }
            break;
        }
        case PCCompareResults::SMALLER:
            CopyReverse(result, resultsFound, resultSize, toContinue, node);
            break;
        case PCCompareResults::EQUAL: {
            uint8_t startLevel = (start.GetKeyLen() > level) ? start[level] : 0;
            N *childNode = N::GetChild(startLevel, node);
            if (childNode != nullptr) {
                if (start[level] != 0) {
                    FindStart(result, resultsFound, resultSize, start, toContinue, childNode, node, level + 1, level);
                } else {
                    FindStart(result, resultsFound, resultSize, start, toContinue, childNode, parentNode, level + 1,
                              parentLevel);
                }
            } else {
                N *child = N::GetLargestChild(node, startLevel);
                if (child != nullptr) {
                    CopyReverse(result, resultsFound, resultSize, toContinue, child);
                } else {
                    childNode = nullptr;
                    if (start[parentLevel] != 0) {
                        childNode = N::GetLargestChild(parentNode, start[parentLevel] - 1);
                    }
                    if (childNode != nullptr) {
                        CopyReverse(result, resultsFound, resultSize, toContinue, childNode);
                    } else {
                        child = N::GetSmallestChild(node, startLevel);
                        Copy(result, resultsFound, resultSize, toContinue, child);
                    }
                }
            }
            break;
        }
        case PCCompareResults::SKIPPED_LEVEL:
            return true;
    }
    return false;
};
void Tree::hashLookup(const Key &k, std::size_t &resultsFound, TID *result){
//    auto prefix = k.extractPrefix(4);
//    ValueEntry* vbuf;
//    auto callback = [&] (HashTable::RecordType record) {vbuf = std::move(record.value());};
//    if(hashAdjTable.Find(prefix,callback)){
//        auto res_node = vbuf->ptid.getVaddr();
//        if(N::IsLeaf(res_node)){
//            result[resultsFound] = N::GetLeaf(res_node);
//            res_count++;
//            resultsFound++;
//
////            }
//        }
//    }
}
TID Tree::LookupNext(const Key &start, ART::ThreadInfo &threadEpochInfo){
    ART::EpochGuardReadonly epochGuard(threadEpochInfo);
    TID toContinue = 0;
    std::size_t resultsFound = 0;
    std::size_t resultSize = 1;
    TID result[resultSize];
//    std::size_t hash_found = 0;
//    auto o0_start_time = std::chrono::steady_clock::now();
//    if(hash_count > 500000){
//        hashLookup(start, hash_found, result);
//    }
//    if(hash_found == 1){
//        auto end_time = std::chrono::steady_clock::now();
//        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - o0_start_time).count();
//        fprintf(stderr, "hash查找时间: %lld na秒\n", duration);
//        return result[0]; // 在找到结果时直接返回
//    }
restart:
    resultsFound = 0;
    uint32_t level = 0;
    NVMPtr<N> nodePtr = root;
    N *node = nodePtr.getVaddr();
    if (FindStart(result, resultsFound, resultSize, start, toContinue, node, node, level, level)) {
        goto restart;
    }
    if (resultsFound == 0) {
        return 0;
    }
    return result[0];
}

}  // namespace ART_ROWEX
