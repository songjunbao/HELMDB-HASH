#include "undo/nvm_undo_segment.h"
#include "undo/nvm_undo_rollback.h"
#include "transaction/nvm_snapshot.h"
#include "nvmdb_thread.h"
#include "common/thread_pool_light.h"
#include <numa.h>

namespace NVMDB {

static UndoSegment *g_undo_segment_padding[NVMDB_UNDO_SEGMENT_NUM + 16];
static UndoSegment **g_undo_segments = &g_undo_segment_padding[16];

// 如果 g_undo_segment_allocated 中某位为true, 则代表有个线程正在持有它, 反之则不一定成立
// 有些用, 但是用处不大, 还得结合segment是否满判断
static bool g_undo_segment_allocated[NVMDB_UNDO_SEGMENT_NUM];

constexpr int SLOT_OFFSET = 2;

thread_local UndoSegment *t_undo_segment = nullptr;
thread_local uint64 t_undo_segment_index = 0;   // the global undo segment index allocated for local thread
static uint64 clock_sweep = 0;
std::mutex g_undoSegmentLock;   // when a thread acquire a new segment

std::thread g_undoRecycle;
static bool g_doRecycle = true;

uint64 UndoSegment::getMaxCSNForRollback() {
    if (isEmpty()) {
        return segHead->m_minSnapshot;
    }
    DCHECK(segHead->m_nextFreeSlot >= 1);
    /* the last 2 allocated tx slot.  */
    uint64 slot_begin = 0;
    uint64 slot_end = segHead->m_nextFreeSlot - 1;
    if (slot_end > 0) {
        slot_begin = slot_end - 1;
    }
    uint64 maxUndoCSN = 0;
    for (uint64 i = slot_begin; i <= slot_end; i++) {   // recovery Tx from [slot begin, slot end]
        TxSlot* txSlot = &segHead->m_txSlots[i % UNDO_TX_SLOTS];
        auto txStatus = txSlot->status;
        auto undoCsn = txSlot->csn;
        if (txStatus == TxSlotStatus::COMMITTED) {    // Tx.csn <= undo_csn must be committed
            maxUndoCSN = std::max(maxUndoCSN, undoCsn);
        }
    }

    if (segHead->m_recoveryStart == 0) {
        /* safe to update recovery restart point; Otherwise means that last crash happens during recovery */
        segHead->m_recoveryStart = slot_begin + 1;
    }
    segHead->m_recoveryEnd = slot_end;
    return maxUndoCSN;
}

void UndoSegment::recycleUndoPages(const uint64 &beginSlot, const uint64 &endSlot) {
    auto segmentSize = m_logicFile.getSegmentSize();
    uint32 startSegmentId = segHead->m_recycledBegin / segmentSize;
    uint32 endSegmentId = 0;
    uint64 recycledEnd = 0;

    DCHECK(beginSlot <= endSlot);
    for (uint64 i = beginSlot; i <= endSlot; i++) {
        TxSlot *txSlot = &segHead->m_txSlots[i % UNDO_TX_SLOTS];
        if (txSlot->start == 0) {
            DCHECK(txSlot->end == 0);
            continue;
        }
        DCHECK(txSlot->end != 0);
        recycledEnd = UndoRecPtrGetOffset(txSlot->end);
        endSegmentId = recycledEnd / segmentSize;
    }
    /* first segment kept as seg header. */
    if (startSegmentId == 0) {
        startSegmentId = 1;
    }
    if (startSegmentId < endSegmentId) {
        segHead->m_recycledBegin = recycledEnd;
        m_logicFile.punch(startSegmentId, endSegmentId);
    }
}

// Any transaction with csn smaller than min_csn (minSnapshot) can be recycled
// Note that this function is invoked by another thread, so deal with shared variables carefully.
// If you want to change instruction order, you'd better check getTxSlot.
void UndoSegment::recycleTxSlot(uint64 minSnapshot) {
    uint64 next_slot = segHead->m_nextRecycleSlot.load(std::memory_order_relaxed);
    uint64 begin_slot = next_slot;
    uint64 max_slot = segHead->m_nextFreeSlot.load(std::memory_order_relaxed);
    bool recycled = false;
    while (next_slot < max_slot) {
        if (!isTxSlotRecyclable(next_slot, minSnapshot)) {
            break;
        }
        next_slot++;
        recycled = true;
    }
    if (recycled) {
        /* Undo recovery need min next available csn to boostrap. */
        if (next_slot + SLOT_OFFSET >= max_slot) {
            DCHECK(segHead->m_minSnapshot <= minSnapshot);
            segHead->m_minSnapshot = minSnapshot;
        }
        /* we must update m_minSlotId first, so any concurrent check will find the slot is recycled
        * (m_minSlotId updated) before the slot is reused (next_recycle_slot updated) */
        segHead->m_minSlotId.store(next_slot, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        /* RE-ORDER NOT ALLOWED HERE. */
        next_slot = segHead->m_minSlotId;
        recycleUndoPages(begin_slot, next_slot - 1);

        DCHECK(next_slot != begin_slot);
        uint64 begin_offset = begin_slot % UNDO_TX_SLOTS;
        uint64 end_offset = next_slot % UNDO_TX_SLOTS;
        if (begin_offset < end_offset) {
            errno_t ret = memset_s(&segHead->m_txSlots[begin_offset], (end_offset - begin_offset) * sizeof(TxSlot),
                                   0, (end_offset - begin_offset) * sizeof(TxSlot));
            SecureRetCheck(ret);
        } else {
            errno_t ret = memset_s(&segHead->m_txSlots[begin_offset], (UNDO_TX_SLOTS - begin_offset) * sizeof(TxSlot),
                                   0, (UNDO_TX_SLOTS - begin_offset) * sizeof(TxSlot));
            SecureRetCheck(ret);
            ret = memset_s(&segHead->m_txSlots[0], end_offset * sizeof(TxSlot),
                           0, end_offset * sizeof(TxSlot));
            SecureRetCheck(ret);
        }

        segHead->m_nextRecycleSlot.store(next_slot, std::memory_order_release);
    }
}

void UndoSegment::backgroundRecovery(UndoRecord* undoRecordCache)  {
    for (uint64 i = segHead->m_recoveryStart; i <= segHead->m_recoveryEnd; i++) {
        auto txSlot = &segHead->m_txSlots[i % UNDO_TX_SLOTS]; // find the first Tx slot for recovery
        auto tx_status = txSlot->status;
        if (tx_status == TxSlotStatus::IN_PROGRESS) {
            UndoRecordRollBack(this, txSlot, undoRecordCache);
            txSlot->status = TxSlotStatus::ROLL_BACKED;
        }
    }
    segHead->m_recoveryStart = 0;
}

void UndoRecycle() { // start a thread with this function
    pthread_setname_np(pthread_self(), "NVM UndoRecycle");
    auto* procArray = ProcessArray::GetGlobalProcArray();
    uint64 previousCSN = MIN_TX_CSN;
    while (g_doRecycle) {
        // 等待 1 毫秒重试
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uint64 nowCSN = procArray->getAndUpdateGlobalMinCSN();
        if (nowCSN == previousCSN) {
            continue;
        }
        previousCSN = nowCSN;
        // 回收废弃的 undo segment
        for (int i = 0; i < NVMDB_UNDO_SEGMENT_NUM; i++) {
            UndoSegment *undoSegment = g_undo_segments[i];
            /* necessary to recycle full undo segment. */
            // 当且仅当
            if (!g_undo_segment_allocated[i] && !undoSegment->isFull()) {
                continue;
            }
            undoSegment->recycleTxSlot(nowCSN);
        }
    }
}

void UndoSegmentCreate() {
    auto threadPoolLight = std::make_unique<util::thread_pool_light>();
    moodycamel::LightweightSemaphore semaphore(0, 0);
    LOG(INFO) << "Start creating undo segments, count: " << NVMDB_UNDO_SEGMENT_NUM;
    auto threadFunc = [&](int idxStart, int idxEnd) {
        // LOG(INFO) << "Start: " << idxStart << ", End: " << idxEnd;
        for (int i = idxStart; i < idxEnd; i++) {  // from 0 to 2048, place undox in a round-robin manner
            g_undo_segments[i] = new UndoSegment(g_dir_config->getDirPathByIndex(i), i);
            g_undo_segments[i]->create();   // create the undo file, map it to memory(segHead)
            g_undo_segment_allocated[i] = false;
        }
        semaphore.signal();
    };
    threadPoolLight->push_loop(0, NVMDB_UNDO_SEGMENT_NUM, threadFunc);
    for (auto i=threadPoolLight->get_thread_count(); i>0; i-=(int)semaphore.waitMany((ssize_t)i));
    LOG(INFO) << "Finish creating undo segments.";
    threadPoolLight.reset(nullptr);
    g_undoRecycle = std::thread(UndoRecycle);   // start nvm global UndoRecycle thread
}

// 在挂载完成后异步后台恢复所有的Undo segment
void UndoBGRecovery() {
    char undoRecordCache[MAX_UNDO_RECORD_CACHE_SIZE]; // 4kb
    // this thread acts as an ordinary worker thread to recover all uncommitted transactions
    InitThreadLocalVariables();
    for (int i = 0; i < NVMDB_UNDO_SEGMENT_NUM; i++) {  // recover all undo segments
        auto segment = g_undo_segments[i];
        segment->backgroundRecovery(reinterpret_cast<UndoRecord *>(undoRecordCache));
    }
    LOG(INFO) << "NVMDB Finish recovered undo segments in background.";
    DestroyThreadLocalVariables();
    UndoRecycle();
}

/* must be invoked after undo tablespace is mounted */
void UndoSegmentMount() {
    uint64 maxUndoCSN = MIN_TX_CSN;
    for (int i = 0; i < NVMDB_UNDO_SEGMENT_NUM; i++) { // there are 2048 global undo segments (undo0-2048)
        g_undo_segments[i] = new UndoSegment(g_dir_config->getDirPathByIndex(i), i);
        g_undo_segments[i]->mount(); // mount undox.0-undox.y
        g_undo_segment_allocated[i] = false;
        uint64 segmentMaxUndoCSN = g_undo_segments[i]->getMaxCSNForRollback();
        maxUndoCSN = std::max(maxUndoCSN, segmentMaxUndoCSN);
        // maxUndoCSN is updated during Recovery
        // 1. set recovery_start and recovery_end for each undo segment
        // 2. update maxUndoCSN to recovery g_commitSequenceNumber
    }

    ProcessArray::GetGlobalProcArray()->setRecoveredCSN(maxUndoCSN);
    LOG(INFO) << "NVMDB Finish initialize undo segments.";
    // the recycle thread will do the recovery first
    g_undoRecycle = std::thread(UndoBGRecovery);
}

void UndoSegmentUnmount() {
    g_doRecycle = false;
    g_undoRecycle.join();
    for (int i = 0; i < NVMDB_UNDO_SEGMENT_NUM; i++) {
        DCHECK(!g_undo_segment_allocated[i]);
        g_undo_segments[i]->unmount();
        delete g_undo_segments[i];
        g_undo_segments[i] = nullptr;
    }
    clock_sweep = 0;
}

UndoSegment *GetUndoSegment(int segId) {
    return g_undo_segments[segId];
}

void InitLocalUndoSegment() {
    if (t_undo_segment == nullptr) {   // thread local undo segment
        g_undoSegmentLock.lock();
        while (true) {
            clock_sweep++;
            t_undo_segment_index = clock_sweep % NVMDB_UNDO_SEGMENT_NUM;
            if (g_undo_segment_allocated[t_undo_segment_index]) {   // retry until finding a new unallocated undo segment
                continue;
            }
            t_undo_segment = g_undo_segments[t_undo_segment_index];

            if (t_undo_segment->getSegmentId() % g_dir_config->size() != GetCurrentGroupId() % g_dir_config->size()) {
                continue;   // the numa node where the thread is running must be the same as the numa node where the storage is.
            }
            if (t_undo_segment->isFull()) {    // skip full segment
                continue;
            }
            g_undo_segment_allocated[t_undo_segment_index] = true;
            // mark the segment as occupied.
            // g_undo_segment_allocated is in memory!
            break;
        }
        g_undoSegmentLock.unlock();
        // bind thread to numa node
        auto groupId = GetCurrentGroupId();
        // LOG(INFO) << "Thread running in numa node id: " << groupId;
        DCHECK(numa_available() >= 0);
        DCHECK(groupId < numa_num_configured_nodes());
        // 总共cpu数量
        bitmask * bits = numa_allocate_cpumask();
        if (numa_node_to_cpus(groupId, bits) == -1) {
            CHECK(false) << "Bind failed!";
        }
        // 绑核, 需要添加 CAP_SYS_NICE 能力 sudo setcap cap_sys_nice=+ep /path/to/your/program
        numa_bind(bits);
        numa_bitmask_free(bits);
    }
}

void DestroyLocalUndoSegment() {
    if (t_undo_segment != nullptr) {
        /* todo: undo_segment can be reused by other threads */
        t_undo_segment = nullptr;
        g_undo_segment_allocated[t_undo_segment_index] = false;
        // free the current undo segment
        // g_undo_segment_allocated is in memory!
    }
}

UndoSegment *GetThreadLocalUndoSegment() {
    DCHECK(t_undo_segment != nullptr);
    return t_undo_segment;
}

void SwitchUndoSegmentIfFull() {
    DCHECK(t_undo_segment != nullptr);
    if (!t_undo_segment->isFull()) {
        return;
    }
    DestroyLocalUndoSegment();
    InitLocalUndoSegment(); // find a new empty segment
    DCHECK(!t_undo_segment->isFull());
}

}  // namespace NVMDB