#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

#include "turbo/turbo_hash.h"
#include "util/hash_function.h"
#include "util/time.h"

// static int kThreadIDs[16] = {0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23};
std::string print_binary (uint32_t bitmap) {
    char buffer[1024];
    const char* bit_rep[16] = {
        [0] = "0000",  [1] = "0001",  [2] = "0010",  [3] = "0011",  [4] = "0100",  [5] = "0101",
        [6] = "0110",  [7] = "0111",  [8] = "1000",  [9] = "1001",  [10] = "1010", [11] = "1011",
        [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
    };
    sprintf (buffer, "%s%s%s%s%s%s%s%s", bit_rep[(bitmap >> 28) & 0x0F],
             bit_rep[(bitmap >> 24) & 0x0F], bit_rep[(bitmap >> 20) & 0x0F],
             bit_rep[(bitmap >> 16) & 0x0F], bit_rep[(bitmap >> 12) & 0x0F],
             bit_rep[(bitmap >> 8) & 0x0F], bit_rep[(bitmap >> 4) & 0x0F],
             bit_rep[(bitmap >> 0) & 0x0F]);
    return buffer;
};

const int kThreadNum = 16;
int main () {
    unsigned* lock_value = (unsigned*)malloc (4);
    // for (int i = 0; i < 32; ++i) {
    //     printf ("test and set bit: %d\n", i);
    //     printf ("\tlock_value before: %09x\n", *lock_value);
    //     char test_set_res = turbo::util::AtomicBitOps::BitTestAndSet (lock_value, i);
    //     printf ("\tlock_value  after: %09x. test&set result: %d\n", *lock_value, test_set_res);
    //     test_set_res = turbo::util::AtomicBitOps::BitTestAndSet (lock_value, i);
    //     printf ("\tlock_value  after: %09x. test&set result: %d (retry)\n", *lock_value,
    //             test_set_res);
    //     char test_reset_res = turbo::util::AtomicBitOps::BitTestAndReset (lock_value, i);
    //     printf ("\tlock_value  after: %09x. test&reset result: %d (reset)\n", *lock_value,
    //             test_reset_res);
    //     test_reset_res = turbo::util::AtomicBitOps::BitTestAndReset (lock_value, i);
    //     printf ("\tlock_value  after: %09x. test&reset result: %d (retry)\n", *lock_value,
    //             test_reset_res);
    // }

    *lock_value = 0xFCFCDAC8;
    turbo::util::turbo_bit_spin_lock (lock_value, 0);
    printf ("succ lock. lock value: %08x\n", *lock_value);

    bool res = turbo::util::turbo_bit_spin_try_lock (lock_value, 0);
    printf ("should fail to try lock the same position. %s\n", res ? "succ" : "fail");

    turbo::util::turbo_bit_spin_unlock (lock_value, 0);
    printf ("succ unlock. lock value: %08x\n", *lock_value);

    int kLoops = 100000;

    {
        std::atomic<int> tmp (0);
        std::vector<std::thread> workers2;
        auto start_time = util::NowNanos ();
        for (int i = 0; i < kThreadNum; i++) {
            // each worker add 10000,
            workers2.push_back (std::thread ([kLoops, &tmp] () {
                // util::util::PinCore(kThreadIDs[i]);
                int loop = kLoops;
                while (loop--) {
                    tmp.fetch_add (1, std::memory_order_relaxed);
                }
            }));
        }
        std::for_each (workers2.begin (), workers2.end (), [] (std::thread& t) { t.join (); });
        auto end_time = util::NowNanos ();
        printf ("fetchadd Speed: %f Mops/s. sum: %d\n",
                (double)(kLoops * kThreadNum) / (end_time - start_time) * 1000.0, tmp.load ());
    }

    {
        int sum = 0;
        std::vector<std::thread> workers2;
        turbo::util::AtomicSpinLock locks;
        auto start_time = util::NowNanos ();
        for (int i = 0; i < kThreadNum; i++) {
            // each worker add 10000,
            workers2.push_back (std::thread ([&locks, kLoops, &sum] () {
                int loop = kLoops;
                while (loop--) {
                    locks.lock ();
                    sum++;
                    locks.unlock ();
                }
            }));
        }
        std::for_each (workers2.begin (), workers2.end (), [] (std::thread& t) { t.join (); });
        auto end_time = util::NowNanos ();
        printf ("AtomicSpinLock Speed: %f Mops/s. sum: %d\n",
                (double)(kLoops * kThreadNum) / (end_time - start_time) * 1000.0, sum);
    }

    {
        *lock_value = 0;
        int sum = 0;
        std::vector<std::thread> workers;
        auto start_time = util::NowNanos ();

        for (int i = 0; i < kThreadNum; i++) {
            // each worker add 10000,
            workers.push_back (std::thread ([kLoops, &lock_value, &sum] () {
                // util::util::PinCore(kThreadIDs[i]);
                size_t loop = kLoops;
                while (loop--) {
                    turbo::util::SpinLockScope<0> lock_scope ((uint32_t*)lock_value);
                    sum++;
                }
            }));
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });
        auto end_time = util::NowNanos ();
        printf ("SpinLockScope Speed: %f Mops/s. sum: %d\n",
                (double)(kLoops * kThreadNum) / (end_time - start_time) * 1000.0, sum);
    }
}