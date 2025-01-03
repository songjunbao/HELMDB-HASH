#include <immintrin.h>

#include <cstdlib>

#include "gflags/gflags.h"
#include "libpmem.h"
#include "pptr.hpp"
#include "ralloc.hpp"
#include "util/hash_function.h"
#include "util/perf_util.h"
#include "util/pmm_util.h"

#define IS_PMEM 1

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;
using namespace util;

DEFINE_int32 (loop, 4, "");
DEFINE_string (filename, "motivation.csv", "");

const uint64_t MASK64 = (~(UINT64_C (63)));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

static uint64_t kBuff = 123;

inline void  // __attribute__((optimize("O0"),always_inline))
RndAccess (char* addr, uint64_t size_mask, uint64_t interval) {
    uint64_t off = turbo::wyhash32 () & size_mask;
    int loop = FLAGS_loop;
    while (loop--) {
        volatile uint64_t tmp = *(uint64_t*)(addr + off);
        kBuff = tmp;
        off += interval;
        off &= size_mask;
    }
}

inline void  // __attribute__((optimize("O0"),always_inline))
ConAccess (char* addr, uint64_t size_mask) {
    uint64_t off = turbo::wyhash32 () & size_mask;
    int loop = FLAGS_loop;
    while (loop--) {
        volatile uint64_t tmp = *(uint64_t*)(addr + off);
        kBuff = tmp;
        off += 64;
    }
}

inline void  // __attribute__((optimize("O0"),always_inline))
RndWrite (char* addr, uint64_t size_mask, uint64_t interval) {
    uint64_t off = turbo::wyhash32 () & size_mask;
    int loop = FLAGS_loop;
    while (loop--) {
        uint64_t val = *(uint64_t*)(addr + off);
        *(uint64_t*)(addr + off) = val + off;
#ifdef IS_PMEM
        FLUSH (addr + off);
#endif
        off += interval;
        off &= size_mask;
    }
#ifdef IS_PMEM
    FLUSHFENCE;
#endif
}

inline void  // __attribute__((optimize("O0"),always_inline))
ConWrite (char* addr, uint64_t size_mask) {
    uint64_t off = turbo::wyhash32 () & size_mask;
    int loop = FLAGS_loop;
    while (loop--) {
        uint64_t val = *(uint64_t*)(addr + off);
        *(uint64_t*)(addr + off) = val + off;
#ifdef IS_PMEM
        FLUSH (addr + off);
#endif
        off += 64;
    }
#ifdef IS_PMEM
    FLUSHFENCE;
#endif
}
#pragma GCC diagnostic pop

void AccessCacheLineSize () {
    const uint64_t repeat = 10000000;
    const uint64_t size = 4LU << 30;
    const uint64_t size_mask = (size - 1) & MASK64;
    uint64_t size_mask2 = (size - 1) & (~(64 * FLAGS_loop - 1));
    uint64_t interval = size / FLAGS_loop;
    char* addr = nullptr;
#ifdef IS_PMEM
    size_t file_size = size;
    std::string filename = "/mnt/pmem/pmem_test.data";
    char* pmem_addr = nullptr;
    size_t mapped_len;
    int is_pmem;
    {
        // util::IPMWatcher watcher("pmem");
        if ((pmem_addr = (char*)pmem_map_file (filename.c_str (), file_size, PMEM_FILE_CREATE, 0666,
                                               &mapped_len, &is_pmem)) == NULL) {
            perror ("pmem_map file creation fail");
            exit (1);
        } else {
            pmem_memset (pmem_addr, 0, file_size, PMEM_F_MEM_NONTEMPORAL);
        }

        addr = pmem_addr;
    }
#else
    addr = (char*)aligned_alloc (64, size);
    memset (addr, 0, size);
#endif

    auto file = fopen (FLAGS_filename.c_str (), "a");
    fprintf (file, "%d, ", FLAGS_loop);

    {
#ifdef IS_PMEM
        IPMWatcher watcher ("rnd_read");
#endif
        util::PCMMetric metric ("rnd_read");
        debug_perf_switch ();
        auto time_start = util::NowNanos ();
        for (uint64_t i = 0; i < repeat; i++) {
            RndAccess (addr, size_mask, interval);
        }
        auto time_end = util::NowNanos ();
        double duration = time_end - time_start;
        fprintf (file, "%f, ", duration / repeat);
        fprintf (stderr, "%lu\r", kBuff);
    }

    {
#ifdef IS_PMEM
        IPMWatcher watcher ("seq_read");
#endif
        util::PCMMetric metric ("seq_read");
        debug_perf_switch ();
        auto time_start = util::NowNanos ();
        for (uint64_t i = 0; i < repeat; i++) {
            ConAccess (addr, size_mask2);
        }
        auto time_end = util::NowNanos ();
        double duration = time_end - time_start;
        fprintf (file, "%f, ", duration / repeat);
        fprintf (stderr, "%lu\r", kBuff);
    }

    {
#ifdef IS_PMEM
        IPMWatcher watcher ("rnd_write");
#endif
        util::PCMMetric metric ("rnd_write");
        debug_perf_switch ();
        auto time_start = util::NowNanos ();
        for (uint64_t i = 0; i < repeat; i++) {
            RndWrite (addr, size_mask, interval);
        }
        auto time_end = util::NowNanos ();
        double duration = time_end - time_start;
        fprintf (file, "%f, ", duration / repeat);
        fprintf (stderr, "%lu\r", kBuff);
    }

    {
#ifdef IS_PMEM
        IPMWatcher watcher ("seq_write");
#endif
        util::PCMMetric metric ("seq_write");
        debug_perf_switch ();
        auto time_start = util::NowNanos ();
        for (uint64_t i = 0; i < repeat; i++) {
            ConWrite (addr, size_mask2);
        }
        auto time_end = util::NowNanos ();
        double duration = time_end - time_start;
        fprintf (file, "%f, ", duration / repeat);
        fprintf (stderr, "%lu\r", kBuff);
    }
    debug_perf_stop ();
    fflush (file);
    fclose (file);

#ifdef IS_PMEM
    pmem_unmap (addr, file_size);
#endif
}

int main (int argc, char* argv[]) {
    ParseCommandLineFlags (&argc, &argv, true);
    debug_perf_ppid ();
    // Contiguous access is more efficient
    AccessCacheLineSize ();
    return 0;
}