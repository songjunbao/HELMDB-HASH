//     ________  ______  ____  ____     __  _____   _____ __  __
//    /_  __/ / / / __ \/ __ )/ __ \   / / / /   | / ___// / / /
//     / / / / / / /_/ / __  / / / /  / /_/ / /| | \__ \/ /_/ /
//    / / / /_/ / _, _/ /_/ / /_/ /  / __  / ___ |___/ / __  /
//   /_/  \____/_/ |_/_____/\____/  /_/ /_/_/  |_/____/_/ /_/
//
//  Fast concurrent hashtable for persistent memory
//  version 1.0.0
//
// MIT License
//
// Copyright (c) 2020 Xingsheng Zhao <xingshengzhao@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef TURBO_HASH_PMEM_H_
#define TURBO_HASH_PMEM_H_

#include <error.h>
#include <immintrin.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "turbo_epoche.h"
// pmem library
#include "pptr.hpp"
#include "ralloc.hpp"

#define TURBO_PMEM_LOG_SIZE ((96LU << 30))

// #define PIN_KEY_TO_THREAD

// Linear probing setting
static const int kTurboPmemMaxProbeLen = 15;
static const int kTurboPmemProbeStep = 1;

#define TURBO_PMEM_LIKELY(x) (__builtin_expect (!!(x), 1))
#define TURBO_PMEM_UNLIKELY(x) (__builtin_expect (!!(x), 0))

#define TURBO_PMEM_BARRIER() asm volatile("" : : : "memory") /* Compile read-write barrier */
#define TURBO_PMEM_CPU_RELAX()                                                \
    asm volatile("pause\n"                                                    \
                 :                                                            \
                 :                                                            \
                 : "memory") /* Pause instruction to prevent excess processor \
                                bus usage */

#define TURBO_PMEM_SPINLOCK_FREE ((0))

inline void TURBO_PMEM_COMPILER_FENCE () { asm volatile("" : : : "memory"); /* Compiler fence. */ }

#define TURBO_PMEM_CACHE_LINE_SIZE ((64))
inline void TURBO_PMEM_CLFLUSH (char* data, int len) {
    volatile char* ptr = (char*)((unsigned long)data & ~(TURBO_PMEM_CACHE_LINE_SIZE - 1));
    for (; ptr < data + len; ptr += TURBO_PMEM_CACHE_LINE_SIZE) {
        FLUSH ((void*)ptr);
    }
    FLUSHFENCE;
}
// turbo_hash logging
namespace {

static inline std::string TURBO_PMEM_CMD (const std::string& content) {
    std::array<char, 128> buffer;
    std::string result;
    auto cmd = "echo '" + content + "' >> turbo_pmem.log";
    std::unique_ptr<FILE, decltype (&pclose)> pipe (popen (cmd.c_str (), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error ("popen() failed!");
    }
    while (fgets (buffer.data (), buffer.size (), pipe.get ()) != nullptr) {
        result += buffer.data ();
    }
    return result;
}

#define TURBO_PMEM_LOG(M, x)        \
    do {                            \
        std::ostringstream ss;      \
        ss << M << x;               \
        TURBO_PMEM_CMD (ss.str ()); \
    } while (0);

#define __FILENAME__ ((strrchr (__FILE__, '/') ? strrchr (__FILE__, '/') + 1 : __FILE__))

#if defined TURBO_PMEM_ENABLE_LOGGING
#define TURBO_PMEM_INFO(x)                                                         \
    do {                                                                           \
        char buffer[1024] = "[ INFO] ";                                            \
        sprintf (buffer + 8, "[%s %s:%d] ", __FILENAME__, __FUNCTION__, __LINE__); \
        TURBO_PMEM_LOG (buffer, x);                                                \
    } while (0);
#else
#define TURBO_PMEM_INFO(x) \
    do {                   \
    } while (0);
#endif

#if !defined NDEBUG && defined TURBO_PMEM_ENABLE_LOGGING
#define TURBO_PMEM_DEBUG(x)                                                        \
    do {                                                                           \
        char buffer[1024] = "[DEBUG] ";                                            \
        sprintf (buffer + 8, "[%s %s:%d] ", __FILENAME__, __FUNCTION__, __LINE__); \
        TURBO_PMEM_LOG (buffer, x);                                                \
    } while (0);
#else
#define TURBO_PMEM_DEBUG(x) \
    do {                    \
    } while (0);
#endif

#if defined TURBO_PMEM_ENABLE_LOGGING
#define TURBO_PMEM_ERROR(x)                                                        \
    do {                                                                           \
        char buffer[1024] = "[ERROR] ";                                            \
        sprintf (buffer + 8, "[%s %s:%d] ", __FILENAME__, __FUNCTION__, __LINE__); \
        TURBO_PMEM_LOG (buffer, x);                                                \
    } while (0);
#else
#define TURBO_PMEM_ERROR(x) \
    do {                    \
    } while (0);
#endif

#if defined TURBO_PMEM_ENABLE_LOGGING
#define TURBO_PMEM_WARNING(x)                                                                    \
    do {                                                                                         \
        char buffer[1024] = "[ WARN] ";                                                          \
        sprintf (buffer + strlen (buffer), "[%s %s:%d] ", __FILENAME__, __FUNCTION__, __LINE__); \
        TURBO_PMEM_LOG (buffer, x);                                                              \
    } while (0);
#else
#define TURBO_PMEM_WARNING(x) \
    do {                      \
    } while (0);
#endif

};  // namespace

namespace turbo_pmem {

using namespace epoche;

namespace util {

// Returns the number of micro-seconds since some fixed point in time. Only
// useful for computing deltas of time.
inline uint64_t NowMicros () {
    static constexpr uint64_t kUsecondsPerSecond = 1000000;
    struct ::timeval tv;
    ::gettimeofday (&tv, nullptr);
    return static_cast<uint64_t> (tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
}
// Returns the number of nano-seconds since some fixed point in time. Only
// useful for computing deltas of time in one run.
// Default implementation simply relies on NowMicros.
// In platform-specific implementations, NowNanos() should return time points
// that are MONOTONIC.
inline uint64_t NowNanos () {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t> (ts.tv_sec) * 1000000000L + ts.tv_nsec;
}

inline bool isPowerOfTwo (uint32_t n) { return (n != 0 && __builtin_popcount (n) == 1); }

/** BitSet
 *  @note: used for bitmap testing
 *  @example:
 *  BitSet bitset(0x05);
 *  for (int i : bitset) {
 *      printf("i: %d\n", i);
 *  }
 *  This will print out 0, 2
 */
class BitSet {
public:
    BitSet () : bits_ (0) {}

    explicit BitSet (uint32_t bits) : bits_ (bits) {}

    BitSet (const BitSet& b) = default;

    inline int validCount (void) { return __builtin_popcount (bits_); }

    inline BitSet& operator++ () {
        // remove the lowest 1-bit
        bits_ &= (bits_ - 1);
        return *this;
    }

    inline explicit operator bool () const { return bits_ != 0; }

    inline int operator* () const {
        // count the tailing zero bit
        return __builtin_ctz (bits_);
    }

    inline BitSet begin () const { return *this; }

    inline BitSet end () const { return BitSet (0); }

    inline uint32_t bit () { return bits_; }

private:
    friend bool operator== (const BitSet& a, const BitSet& b) { return a.bits_ == b.bits_; }
    friend bool operator!= (const BitSet& a, const BitSet& b) { return a.bits_ != b.bits_; }
    uint32_t bits_;
};  // end of class BitSet

/** Slice
 *  @note: Derived from LevelDB. the data is stored in the *data_
 */
class Slice {
public:
    using type = Slice;
    // operator <
    bool operator< (const Slice& b) const { return compare (b) < 0; }

    bool operator> (const Slice& b) const { return compare (b) > 0; }

    // explicit conversion
    inline operator std::string () const { return std::string (data_, size_); }

    // Create an empty slice.
    Slice () : data_ (""), size_ (0) {}

    // Create a slice that refers to d[0,n-1].
    Slice (const char* d, size_t n) : data_ (d), size_ (n) {}

    // Create a slice that refers to the contents of "s"
    Slice (const std::string& s) : data_ (s.data ()), size_ (s.size ()) {}

    // Create a slice that refers to s[0,strlen(s)-1]
    Slice (const char* s) : data_ (s), size_ ((s == nullptr) ? 0 : strlen (s)) {}

    // Return a pointer to the beginning of the referenced data
    inline const char* data () const { return data_; }

    // Return the length (in bytes) of the referenced data
    inline size_t size () const { return size_; }

    // Return true iff the length of the referenced data is zero
    inline bool empty () const { return size_ == 0; }

    // Return the ith byte in the referenced data.
    // REQUIRES: n < size()
    inline char operator[] (size_t n) const {
        assert (n < size ());
        return data_[n];
    }

    // Change this slice to refer to an empty array
    inline void clear () {
        data_ = "";
        size_ = 0;
    }

    inline std::string ToString () const {
        std::string res;
        res.assign (data_, size_);
        return res;
    }

    // Three-way comparison.  Returns value:
    //   <  0 iff "*this" <  "b",
    //   == 0 iff "*this" == "b",
    //   >  0 iff "*this" >  "b"
    inline int compare (const Slice& b) const {
        assert (data_ != nullptr && b.data_ != nullptr);
        const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
        int r = memcmp (data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_)
                r = -1;
            else if (size_ > b.size_)
                r = +1;
        }
        return r;
    }

    friend std::ostream& operator<< (std::ostream& os, const Slice& str) {
        os << str.ToString ();
        return os;
    }

    const char* data_;
    size_t size_;
};  // end of class Slice

inline bool operator== (const Slice& x, const Slice& y) {
    return ((x.size () == y.size ()) && (memcmp (x.data (), y.data (), x.size ()) == 0));
}

inline bool operator!= (const Slice& x, const Slice& y) { return !(x == y); }

/** Hasher
 *  @note: provide hash function for string
 */
class Hasher {
public:
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using uint128_t = unsigned __int128;
#pragma GCC diagnostic pop
#endif

    static inline uint64_t umul128 (uint64_t a, uint64_t b, uint64_t* high) noexcept {
        auto result = static_cast<uint128_t> (a) * static_cast<uint128_t> (b);
        *high = static_cast<uint64_t> (result >> 64U);
        return static_cast<uint64_t> (result);
    }

    static inline size_t hash (const void* key, int len) { return ((MurmurHash64A (key, len))); }

    static inline size_t hash_int (uint64_t obj) noexcept {
        // 167079903232 masksum, 120428523 ops best: 0xde5fb9d2630458e9
        static constexpr uint64_t k = UINT64_C (0xde5fb9d2630458e9);
        uint64_t h;
        uint64_t l = umul128 (obj, k, &h);
        return h + l;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    static inline uint64_t MurmurHash64A (const void* key, int len) {
        const uint64_t m = UINT64_C (0xc6a4a7935bd1e995);
        const uint64_t seed = UINT64_C (0xe17a1465);
        const int r = 47;

        uint64_t h = seed ^ (len * m);

        const uint64_t* data = (const uint64_t*)key;
        const uint64_t* end = data + (len / 8);

        while (data != end) {
            uint64_t k = *data++;

            k *= m;
            k ^= k >> r;
            k *= m;

            h ^= k;
            h *= m;
        }

        const unsigned char* data2 = (const unsigned char*)data;

        switch (len & 7) {
            case 7:
                h ^= ((uint64_t)data2[6]) << 48;
            case 6:
                h ^= ((uint64_t)data2[5]) << 40;
            case 5:
                h ^= ((uint64_t)data2[4]) << 32;
            case 4:
                h ^= ((uint64_t)data2[3]) << 24;
            case 3:
                h ^= ((uint64_t)data2[2]) << 16;
            case 2:
                h ^= ((uint64_t)data2[1]) << 8;
            case 1:
                h ^= ((uint64_t)data2[0]);
                h *= m;
        };

        h ^= h >> r;
        h *= m;
        h ^= h >> r;

        return h;
    }
#pragma GCC diagnostic pop

};  // end fo class Hasher

/** AtomicBitOps
 *  @note: provide atomic bit test-and-set, test-and-reset
 */
class AtomicBitOps {
public:
    /** https://stackoverflow.com/questions/30467638/cheapest-least-intrusive-way-to-atomically-update-a-bit
     * \brief Atomically tests and sets a bit (INTEL only)
     * \details Sets bit \p bit of *\p ptr and returns its previous value.
     * The function is atomic and acts as a read-write memory barrier.
     * \param[in] ptr a pointer to an unsigned integer
     * \param[in] bit index of the bit to set in *\p ptr
     * \return the previous value of bit \p bit
     */
    static inline char BitTestAndSet (volatile unsigned int* ptr, unsigned int bit) {
        char out;
#if defined(__x86_64)
        __asm__ __volatile__(
            "lock; bts %2,%1\n"  // set carry flag if bit %2 (bit) of %1 (ptr) is set
                                 //   then set bit %2 of %1
            "sbb %0,%0\n"        // set %0 (out) if carry flag is set
            : "=r"(out), "=m"(*ptr)
            : "Ir"(bit)
            : "memory");
#else
        __asm__ __volatile__(
            "lock; bts %2,%1\n"  // set carry flag if bit %2 (bit) of %1 (ptr) is set
                                 //   then set bit %2 of %1
            "sbb %0,%0\n"        // set %0 (out) if carry flag is set
            : "=q"(out), "=m"(*ptr)
            : "Ir"(bit)
            : "memory");
#endif
        return out;
    }

    /**
     * \brief Atomically tests and resets a bit (INTEL only)
     * \details Resets bit \p bit of *\p ptr and returns its previous value.
     * The function is atomic and acts as a read-write memory barrier
     * \param[in] ptr a pointer to an unsigned integer
     * \param[in] bit index of the bit to reset in \p ptr
     * \return the previous value of bit \p bit
     */
    static inline char BitTestAndReset (volatile unsigned int* ptr, unsigned int bit) {
        char out;
#if defined(__x86_64)
        __asm__ __volatile__(
            "lock; btr %2,%1\n"  // set carry flag if bit %2 (bit) of %1 (ptr) is set
                                 //   then reset bit %2 of %1
            "sbb %0,%0\n"        // set %0 (out) if carry flag is set
            : "=r"(out), "=m"(*ptr)
            : "Ir"(bit)
            : "memory");
#else
        __asm__ __volatile__(
            "lock; btr %2,%1\n"  // set carry flag if bit %2 (bit) of %1 (ptr) is set
                                 //   then reset bit %2 of %1
            "sbb %0,%0\n"        // set %0 (out) if carry flag is set
            : "=q"(out), "=m"(*ptr)
            : "Ir"(bit)
            : "memory");
#endif
        return out;
    }

};  // end of class AtomicBitOps

static inline bool turbo_bit_spin_try_lock (uint32_t* lock, int bit_pos) {
    return AtomicBitOps::BitTestAndSet (lock, bit_pos) == TURBO_PMEM_SPINLOCK_FREE;
}
static inline bool turbo_lockbusy (uint32_t* lock, int bit_pos) {
    uint32_t lock_value = __atomic_load_n (lock, __ATOMIC_ACQUIRE);
    return lock_value & (1 << bit_pos);
}
static inline void turbo_bit_spin_lock (uint32_t* lock, int bit_pos) {
    while (1) {
        // test & set return 0 if success
        if (AtomicBitOps::BitTestAndSet (lock, bit_pos) == TURBO_PMEM_SPINLOCK_FREE) {
            return;
        }
        while (turbo_lockbusy (lock, bit_pos)) TURBO_PMEM_CPU_RELAX ();
    }
}
static inline void turbo_bit_spin_unlock (uint32_t* lock, int bit_pos) {
    TURBO_PMEM_BARRIER ();
    *lock &= ~(1 << bit_pos);
}

/** SpinLockScope
 *  @note: a spinlock monitor, lock when initialized, unlock then deconstructed.
 */
template <int kBitLockPosition>
class SpinLockScope {
public:
    SpinLockScope (char* addr) : lock_ (reinterpret_cast<uint32_t*> (addr)) {
        // lock the bit lock
        turbo_bit_spin_lock (lock_, kBitLockPosition);
    }

    SpinLockScope (uint32_t* lock) : lock_ (lock) {
        // lock the bit lock
        turbo_bit_spin_lock (lock_, kBitLockPosition);
    }

    ~SpinLockScope () {
        // release the bit lock
        turbo_bit_spin_unlock (lock_, kBitLockPosition);
    }

private:
    uint32_t* lock_;
};  // end of class SpinLockScope

// https://rigtorp.se/spinlock/
class AtomicSpinLock {
public:
    std::atomic<bool> lock_ = {0};

    void inline lock () noexcept {
        for (;;) {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange (true, std::memory_order_acquire)) {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load (std::memory_order_relaxed)) {
                // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                // hyper-threads
                __builtin_ia32_pause ();
            }
        }
    }

    bool inline is_locked (void) noexcept { return lock_.load (std::memory_order_relaxed); }

    bool inline try_lock () noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load (std::memory_order_relaxed) &&
               !lock_.exchange (true, std::memory_order_acquire);
    }

    void inline unlock () noexcept { lock_.store (false, std::memory_order_release); }
};  // end of class AtomicSpinLock

};  // namespace util

// A thin wrapper around std::hash, performing an additional simple mixing step
// of the result. from https://github.com/martinus/robin-hood-hashing
template <typename T>
struct hash : public std::hash<T> {
    size_t operator() (T const& obj) const
        noexcept (noexcept (std::declval<std::hash<T>> ().operator() (std::declval<T const&> ()))) {
        // call base hash
        auto result = std::hash<T>::operator() (obj);
        // return mixed of that, to be save against identity has
        return util::Hasher::hash_int (static_cast<uint64_t> (result));
    }
};
template <>
struct hash<std::string> {
    size_t operator() (std::string const& str) const noexcept {
        return util::Hasher::hash (str.data (), str.size ());
    }
};
template <class T>
struct hash<T*> {
    size_t operator() (T* ptr) const noexcept {
        return util::Hasher::hash_int (reinterpret_cast<size_t> (ptr));
    }
};
#define TURBO_HASH_INT(T)                                                \
    template <>                                                          \
    struct hash<T> {                                                     \
        size_t operator() (T obj) const noexcept {                       \
            return util::Hasher::hash_int (static_cast<uint64_t> (obj)); \
        }                                                                \
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif
// see https://en.cppreference.com/w/cpp/utility/hash
TURBO_HASH_INT (bool);
TURBO_HASH_INT (char);
TURBO_HASH_INT (signed char);
TURBO_HASH_INT (unsigned char);
TURBO_HASH_INT (char16_t);
TURBO_HASH_INT (char32_t);
TURBO_HASH_INT (wchar_t);
TURBO_HASH_INT (short);
TURBO_HASH_INT (unsigned short);
TURBO_HASH_INT (int);
TURBO_HASH_INT (unsigned int);
TURBO_HASH_INT (long);
TURBO_HASH_INT (long long);
TURBO_HASH_INT (unsigned long);
TURBO_HASH_INT (unsigned long long);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
// dummy hash, unsed as mixer when turbo_pmem::hash is already used
template <typename T>
struct identity_hash {
    constexpr size_t operator() (T const& obj) const noexcept { return static_cast<size_t> (obj); }
};

namespace detail {

// using wrapper classes for hash and key_equal prevents the diamond problem
// when the same type is used. see https://stackoverflow.com/a/28771920/48181
// from robinhood hash. https://github.com/martinus/robin-hood-hashing
template <typename T>
struct WrapHash : public T {
    WrapHash () = default;
    explicit WrapHash (T const& o) noexcept (noexcept (T (std::declval<T const&> ()))) : T (o) {}
};
template <typename T>
struct WrapKeyEqual : public T {
    WrapKeyEqual () = default;
    explicit WrapKeyEqual (T const& o) noexcept (noexcept (T (std::declval<T const&> ())))
        : T (o) {}
};

/** TurboHashTable
 *  @format:
 *           | bucket 0 | bucket 1 | ... | bucket n |
 *           |  cell 0  |  cell 0  |     |          |
 *           |  cell 1  |  cell 1  |     |          |
 *           |  cell 2  |  cell 2  |     |          |
 *           |    ...   |    ...   |     |          |
 *
 */
template <typename Key, typename T, typename Hash, typename KeyEqual, int kCellCountLimit = 32768>
class TurboHashTable : public WrapHash<Hash>, public WrapKeyEqual<KeyEqual> {
public:
    static constexpr bool is_key_flat = std::is_same<Key, std::string>::value == false;
    static constexpr bool is_value_flat = std::is_same<T, std::string>::value == false;
    // Internal transformation for std::string
    using key_type = Key;
    using mapped_type = T;
    using size_type = size_t;
    using hasher = Hash;

    // private:

    /**
     *  @note: obtain the Record Lenght
     */
    template <bool IsT1Numeric, typename T1>
    struct Record1Size {};

    /**
     *  @note: calcuate the Record Lenght
     */
    template <bool IsT1Numeric, typename T1>
    struct Record1Format {};

    /**
     *  @note: encode T1 to memory
     *  ! allocate memory space in advance
     */
    template <bool IsT1Numeric, typename T1>
    struct EncodeToRecord1 {};

    /** DecodeInRecord1
     *  @note: decode T1
     */
    template <bool IsT1Numeric, typename T1>
    struct DecodeInRecord1 {};

/** Case 1: T1 is numeric
 *  * record memory layout:
 *         |   T1  |
 *         |
 *         |
 *     addr start here
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
    template <typename T1>
    struct Record1Size<true, T1> {
        static inline size_t Size (char* addr) { return sizeof (T1); }
    };
#pragma GCC diagnostic pop

    template <typename T1>
    struct Record1Format<true, T1> {
        static inline constexpr size_t Length (const T1& t1) { return sizeof (t1); }
    };
    template <typename T1>
    struct EncodeToRecord1<true, T1> {
        static inline void Encode (const T1& t1, char* addr) { *reinterpret_cast<T1*> (addr) = t1; }
    };
    template <typename T1>
    struct DecodeInRecord1<true, T1> {
        static inline T1 Decode (char* addr) { return *reinterpret_cast<T1*> (addr); }
    };

    /** Case 2: T1 is std::string
     *  * record memory layout:
     *         |  len1  | buffer1
     *         | size_t |
     *         |
     *     addr start here
     */
    template <typename T1>
    struct Record1Size<false, T1> {
        static inline size_t Size (char* addr) {
            size_t len1 = *reinterpret_cast<size_t*> (addr);
            return sizeof (size_t) + len1;
        }
    };
    template <typename T1>
    struct Record1Format<false, T1> {
        static inline size_t Length (const T1& t1) { return sizeof (size_t) + t1.size (); }
    };
    template <typename T1>
    struct EncodeToRecord1<false, T1> {
        static inline void Encode (const T1& t1, char* addr) {
            *reinterpret_cast<size_t*> (addr) = t1.size ();
            memcpy (addr + sizeof (size_t), t1.data (), t1.size ());
        }
    };
    // When T1 is std::string, 'Decode' return util::Slice, which is the reference
    // of real value.
    template <typename T1>
    struct DecodeInRecord1<false, T1> {
        static inline util::Slice Decode (char* addr) {
            size_t len1 = *reinterpret_cast<size_t*> (addr);
            return util::Slice (addr + sizeof (size_t), len1);
        }
    };

    /**
     *  @note: obtain the Record Lenght
     */
    template <bool IsT1Numeric, bool IsT2Numeric, typename T1, typename T2>
    struct Record2Size {};

    /**
     *  @note: calculate the Record Lenght
     */
    template <bool IsT1Numeric, bool IsT2Numeric, typename T1, typename T2>
    struct Record2Format {};

    /** EncodeToRecord2
     *  @note: encode T1 and T2 to addr.
     *  ! The memory space should be allocated in advance
     */
    template <bool IsT1Numeric, bool IsT2Numeric, typename T1, typename T2>
    struct EncodeToRecord2 {};

    /** DecodeInRecord2
     *  @note: decode T1 and T2
     */
    template <bool IsT1Numeric, bool IsT2Numeric, bool IsFirst, typename T1, typename T2>
    struct DecodeInRecord2 {};

/** Case 1: T1 and T2 are numeric
 *  * record memory layout:
 *         |   T1  |   T2  |
 *         |
 *         |
 *     addr start here
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
    template <typename T1, typename T2>
    struct Record2Size<true, true, T1, T2> {
        static inline size_t Size (char* addr) { return sizeof (T1) + sizeof (T2); }
    };
#pragma GCC diagnostic pop
    template <typename T1, typename T2>
    struct Record2Format<true, true, T1, T2> {
        static inline constexpr size_t Length (const T1& t1, const T2& t2) {
            return sizeof (t1) + sizeof (t2);
        }
    };
    template <typename T1, typename T2>
    struct EncodeToRecord2<true, true, T1, T2> {
        static inline void Encode (const T1& t1, const T2& t2, char* addr) {
            *reinterpret_cast<T1*> (addr) = t1;
            *reinterpret_cast<T2*> (addr + sizeof (T1)) = t2;
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<true, true, true /* IsFirst */, T1, T2> {
        static inline T1 Decode (char* addr) { return *reinterpret_cast<T1*> (addr); }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<true, true, false /* IsFirst*/, T1, T2> {
        static inline T2 Decode (char* addr) { return *reinterpret_cast<T2*> (addr + sizeof (T1)); }
    };

    /** Case 2: T1 is numeric, T2 is std::string
     *  * record memory layout:
     *         |   T1  |   len2  |   buffer2
     *         |       |  size_t |
     *         |                 |
     *     addr start here     offset
     */
    template <typename T1, typename T2>
    struct Record2Size<true, false, T1, T2> {
        static inline size_t Size (char* addr) {
            size_t len2 = *reinterpret_cast<size_t*> (addr + sizeof (T1));
            return sizeof (T1) + sizeof (size_t) + len2;
        }
    };
    template <typename T1, typename T2>
    struct Record2Format<true, false, T1, T2> {
        static inline size_t Length (const T1& t1, const T2& t2) {
            return sizeof (t1) + sizeof (size_t) + t2.size ();
        }
    };
    template <typename T1, typename T2>
    struct EncodeToRecord2<true, false, T1, T2> {
        static inline void Encode (const T1& t1, const T2& t2, char* addr) {
            static constexpr size_t offset = sizeof (T1) + sizeof (size_t);
            memcpy (addr, &t1, sizeof (T1));
            *reinterpret_cast<size_t*> (addr + sizeof (T1)) = t2.size ();
            if (t2.size () != 0) memcpy (addr + offset, t2.data (), t2.size ());
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<true, false, true /* IsFirst*/, T1, T2> {
        static inline T1 Decode (char* addr) { return *reinterpret_cast<T1*> (addr); }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<true, false, false /* IsFirst*/, T1, T2> {
        static inline util::Slice Decode (char* addr) {
            size_t len2 = *reinterpret_cast<size_t*> (addr + sizeof (T1));
            return util::Slice (addr + sizeof (T1) + sizeof (size_t), len2);
        }
    };

    /** Case 3: T1 is std::string, T2 is numeric
     *  * record memory layout:
     *         |  len1  |   T2  |   buffer1
     *         | size_t |       |
     *         |
     *     addr start here
     */
    template <typename T1, typename T2>
    struct Record2Size<false, true, T1, T2> {
        static inline size_t Size (char* addr) {
            size_t len1 = *reinterpret_cast<size_t*> (addr);
            return sizeof (size_t) + sizeof (T2) + len1;
        }
    };
    template <typename T1, typename T2>
    struct Record2Format<false, true, T1, T2> {
        static inline size_t Length (const T1& t1, const T2& t2) {
            return sizeof (size_t) + sizeof (t2) + t1.size ();
        }
    };
    template <typename T1, typename T2>
    struct EncodeToRecord2<false, true, T1, T2> {
        static inline void Encode (const T1& t1, const T2& t2, char* addr) {
            static constexpr size_t offset = sizeof (size_t) + sizeof (T2);
            *reinterpret_cast<size_t*> (addr) = t1.size ();
            memcpy (addr + sizeof (size_t), &t2, sizeof (T2));
            memcpy (addr + offset, t1.data (), t1.size ());
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<false, true, true /* IsFirst*/, T1, T2> {
        static inline util::Slice Decode (char* addr) {
            size_t len1 = *reinterpret_cast<size_t*> (addr);
            return util::Slice (addr + sizeof (size_t) + sizeof (T2), len1);
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<false, true, false /* IsFirst*/, T1, T2> {
        static inline T2 Decode (char* addr) {
            return *reinterpret_cast<T2*> (addr + sizeof (size_t));
        }
    };

    /** Case 4: T1 is std::string, T2 is std::string
     *  * record memory layout:
     *         |  len1  |  len2  | buffer1 | bufer2 |
     *         |   u32  |  u32   |         |
     *         |                 |
     *     addr start here     offset
     */
    template <typename T1, typename T2>
    struct Record2Size<false, false, T1, T2> {
        static inline size_t Size (char* addr) {
            uint32_t len1 = *reinterpret_cast<uint32_t*> (addr);
            uint32_t len2 = *reinterpret_cast<uint32_t*> (addr + sizeof (uint32_t));
            return sizeof (uint32_t) + sizeof (uint32_t) + len1 + len2;
        }
    };
    template <typename T1, typename T2>
    struct Record2Format<false, false, T1, T2> {
        static inline size_t Length (const T1& t1, const T2& t2) {
            return sizeof (uint32_t) + sizeof (uint32_t) + t1.size () + t2.size ();
        }
    };
    template <typename T1, typename T2>
    struct EncodeToRecord2<false, false, T1, T2> {
        static inline void Encode (const T1& t1, const T2& t2, char* addr) {
            static constexpr uint32_t offset = sizeof (uint32_t) + sizeof (uint32_t);
            *reinterpret_cast<uint32_t*> (addr) = t1.size ();
            *reinterpret_cast<uint32_t*> (addr + sizeof (uint32_t)) = t2.size ();
            memcpy (addr + offset, t1.data (), t1.size ());
            if (t2.size () != 0) memcpy (addr + offset + t1.size (), t2.data (), t2.size ());
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<false, false, true /* IsFirst*/, T1, T2> {
        static inline util::Slice Decode (char* addr) {
            uint32_t len1 = *reinterpret_cast<uint32_t*> (addr);
            return util::Slice (addr + sizeof (uint32_t) + sizeof (uint32_t), len1);
        }
    };
    template <typename T1, typename T2>
    struct DecodeInRecord2<false, false, false /* IsFirst*/, T1, T2> {
        static inline util::Slice Decode (char* addr) {
            uint32_t len1 = *reinterpret_cast<uint32_t*> (addr);
            uint32_t len2 = *reinterpret_cast<uint32_t*> (addr + sizeof (uint32_t));
            return util::Slice (addr + sizeof (uint32_t) + sizeof (uint32_t) + len1, len2);
        }
    };

    using H2Tag = uint8_t;
    using H1Tag = typename std::conditional<is_key_flat, Key, uint64_t>::type;
    using Entry = typename std::conditional<is_key_flat && is_value_flat, T, pptr<char>>::type;

    template <typename T1, bool flat_key>
    struct H1Convert {};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
    template <typename T1>
    struct H1Convert<T1, false> {
        inline T1 operator() (const Key& key, uint64_t hash) { return hash; }
    };
    template <typename T1>
    struct H1Convert<T1, true> {
        inline T1 operator() (const Key& key, uint64_t hash) { return key; }
    };
#pragma GCC diagnostic pop

    /** PartialHash
     *  @note: a 64-bit hash is used to locate the cell location, and provide
     * hash-tag.
     *  @format:
     *  | MSB    - - - - - - - - - - - - - - - - - - LSB |
     *  |     32 bit     |   16 bit  |  8 bit  |  8 bit  |
     *  |    bucket_hash |    H2     |                   |
     *  |                     H1                         |
     *  ! if Key is numeric, H1 store the key itself
     */
    struct PartialHash {
        PartialHash (const Key& key, uint64_t hash)
            : H1_ (H1Convert<H1Tag, is_key_flat>{}(key, hash)),
              H2_ ((hash >> 16) & 0xFFFF),
              bucket_hash_ (hash >> 32){};
        H1Tag H1_;              // H1:
        H2Tag H2_;              // H2: 2 byte hash tag
        uint32_t bucket_hash_;  // used to locate in the bucket directory
    };                          // end of class PartialHash

    /** HashSlot
     *  @node:
     */
    struct HashSlot {
        union {
            struct {
                H1Tag H1;
                Entry entry;
            };
            uint64_t _[2];
        };
    };

    template <typename T1, bool key_flat, bool value_flat>
    struct SlotRecord : public HashSlot {};

    using SlotType = SlotRecord<Key, is_key_flat, is_value_flat>;

    /** CellMeta256V2
     *  @note: Hash cell whose size is 256 byte. There are 14 slots in the cell.
     *  @format:
     *  | ------------------- 32 Byte meta --------------------| ----- Slots ----- |
     *  |    4 Bytes   |     4 Bytes     | 8 Bytes  | 16 Bytes |   16 Bytes * 14   |
     *  |  Bitmap Zone | Sequence Number | None     | Hash Tag |
     *
     *  |- Bitmap Zone:
     *      2  - 15 bit: indicate which slot is valid or not
     *      18 - 31 bit: delete_bitmap. 1: deleted
     *
     *  |- Sequence Number: increase for each write
     *
     *  |- Hash Tag
     *      One byte tag (H2) for the slot
     *
     *  |- Slots:
     *      0  -  7 byte: H1 tag or real key for flat_key
     *      8  - 15 byte: pointer to data or read value for flat_value
     */
    class CellMeta256V2 {
    public:
        static constexpr uint16_t BitMapMask = 0xFFFC;
        static constexpr int CellSizeLeftShift = 8;
        static constexpr int SlotSizeLeftShift = 4;

        struct Version {
            explicit Version (uint64_t data) : data_ (data) {}
            explicit Version (uint16_t b, uint16_t bd, uint32_t seq)
                : bitmap_ (b), bitmap_deleted_ (bd), seq_no_ (seq) {}

            Version (const Version& v) { this->data_ = v.data_; }
            union {
                struct {
                    uint16_t bitmap_;
                    uint16_t bitmap_deleted_;
                    uint32_t seq_no_;
                };
                uint64_t data_;
            };

            inline bool IsPosValid (int i) { return (bitmap_ & ~bitmap_deleted_ & (1 << i)) != 0; }

            bool operator== (const Version& v1) { return data_ == v1.data_; }
            bool operator!= (const Version& v1) { return data_ != v1.data_; }
        };

        explicit CellMeta256V2 (char* rep)
            // obtain the hash tags to meta_
            : meta_ (_mm_loadu_si128 (reinterpret_cast<const __m128i*> (rep + 16))),
              ver_ (LoadVersion (rep)) {}

        ~CellMeta256V2 () {}

        static inline __m128i SetHashVec (H2Tag hash) { return _mm_set1_epi8 (hash); }

        static inline Version LoadVersion (char* cell_addr) {
            return Version (__atomic_load_n ((uint64_t*)cell_addr, __ATOMIC_ACQUIRE) &
                            (0xFFFF'FFFF'FFFC'FFFCLU));
        }

        static inline void StoreVersion (char* cell_addr, const Version& v) {
            __atomic_store_n (reinterpret_cast<uint64_t*> (cell_addr), v.data_, __ATOMIC_RELEASE);
        }

        static inline SlotType* LocateSlot (char* cell_addr, int slot_i) {
            return reinterpret_cast<SlotType*> (cell_addr + (slot_i << SlotSizeLeftShift));
        }

        static inline H2Tag* LocateH2Tag (char* cell_addr, int slot_i) {
            return reinterpret_cast<H2Tag*> (cell_addr + 16) + slot_i;
        }

        inline Version GetVersion () { return ver_; }

        inline util::BitSet MatchBitSet (const __m128i& hash_vec) {
#ifdef __AVX512__
            uint16_t mask = _mm_cmpeq_epi8_mask (hash_vec, meta_);
#else
            auto compare_res = _mm_cmpeq_epi8 (hash_vec, meta_);
            uint16_t mask = _mm_movemask_epi8 (compare_res);
#endif
            return util::BitSet (mask & ver_.bitmap_ & ~ver_.bitmap_deleted_ & BitMapMask);
        }

        inline util::BitSet EraseBitSet () {
            return util::BitSet (ver_.bitmap_deleted_ & BitMapMask);
        }

        inline util::BitSet BackupBitSet () { return util::BitSet (~ver_.bitmap_ & BitMapMask); }

        inline util::BitSet ValidBitSet () {
            return util::BitSet (ver_.bitmap_ & ~ver_.bitmap_deleted_ & BitMapMask);
        }

        inline bool IsDeleted (int i) { return (ver_.bitmap_deleted_ >> i) & 0x1; }

        inline bool Full () { return __builtin_popcount (ver_.bitmap_ & BitMapMask) == 13; }

        inline bool Occupy (int slot_index) { return ver_.bitmap_ & (1 << slot_index); }

        inline int OccupyCount () { return __builtin_popcount (ver_.bitmap_); }

        inline static constexpr uint8_t StartSlotPos () { return 2; }

        inline static constexpr uint32_t CellSize () {
            // cell size (include meta) in byte
            return 256;
        }

        inline static constexpr uint32_t SlotMaxRange () {
            // used for rehashing. Though we have 16 slots, one is reserved for update
            // and deletion.
            return 15;
        }

        inline static constexpr uint32_t SlotCount () {
            // slot count
            return 14;
        }

        inline static std::string Name () { return "CellMeta256V2"; }

        inline static constexpr size_t size () {
            // the meta size in byte in current cell
            return 32;
        }

        std::string BitMapToString () {
            std::string res;
            char buffer[1024];
            uint64_t H2s[4];
            memcpy (H2s, &meta_, 32);
            sprintf (buffer, "bitmap: 0b%s, deleted: 0b%s - H2: 0x%016lx%016lx%016lx%016lx",
                     print_binary (ver_.bitmap_).c_str (),
                     print_binary (ver_.bitmap_deleted_).c_str (), H2s[3], H2s[2], H2s[1], H2s[0]);
            return buffer;
        }

        std::string ToString () { return BitMapToString (); }

        std::string print_binary (uint16_t bitmap) {
            char buffer[1024];
            static std::string bit_rep[16] = {"0000", "0001", "0010", "0011", "0100", "0101",
                                              "0110", "0111", "1000", "1001", "1010", "1011",
                                              "1100", "1101", "1110", "1111"};
            sprintf (buffer, "%s%s%s%s", bit_rep[(bitmap >> 12) & 0x0F].c_str (),
                     bit_rep[(bitmap >> 8) & 0x0F].c_str (), bit_rep[(bitmap >> 4) & 0x0F].c_str (),
                     bit_rep[(bitmap >> 0) & 0x0F].c_str ());
            return buffer;
        }

        __m128i meta_;  // 16 byte integer vector for hash tags
        Version ver_;

    };  // end of class CellMeta256V2

    /** ProbeWithinBucket
     *  @note: probe within a bucket
     */
    class ProbeWithinBucket {
    public:
        static const int MAX_PROBE_LEN = kTurboPmemMaxProbeLen;
        static const int PROBE_STEP = kTurboPmemProbeStep;
        ProbeWithinBucket (uint64_t initial_hash, uint32_t cell_count_mask, uint32_t bucket_i) {
            h_ = initial_hash;
            cell_count_mask_ = cell_count_mask;
            cell_index_ = h_ & cell_count_mask_;
            bucket_i_ = bucket_i;
            probe_count_ = 0;
        }

        inline void reset () {
            cell_index_ = h_ & cell_count_mask_;
            probe_count_ = 0;
        }

        // indicate whether we have already probed all the assocaite cells
        inline operator bool () const { return probe_count_ <= cell_count_mask_; }

        inline void next () {
            cell_index_ += PROBE_STEP;
            // CellCountMask should be like 0b11
            cell_index_ &= cell_count_mask_;
            probe_count_++;
        }

        inline std::pair<uint32_t, uint32_t> offset () { return {bucket_i_, cell_index_}; }

        static std::string name () { return "ProbeWithinBucket"; }

    private:
        uint64_t h_;
        uint32_t cell_count_mask_;
        uint32_t cell_index_;
        uint32_t bucket_i_;
        uint32_t probe_count_;
    };

    static_assert (__builtin_popcount (kCellCountLimit) == 1,
                   "kCellCountLimit should be power of two");
    static_assert (kCellCountLimit <= 32768, "kCellCountLimit needs to be <= 32768");

    using CellMeta = CellMeta256V2;
    using WHash = WrapHash<Hash>;
    using WKeyEqual = WrapKeyEqual<KeyEqual>;

    static_assert (sizeof (HashSlot) == 16, "HashSlot size error.");
    /** SlotInfo
     *  @note: use to store the target slot location info
     */
    class SlotInfo {
    public:
        uint32_t bucket;   // bucket index
        uint32_t cell;     // cell index
        uint8_t slot;      // slot index
        uint8_t old_slot;  // if equal_key, this save old slot position
        H1Tag H1;          // hash-tag in HashSlot
        H2Tag H2;          // hash-tag in CellMeta256V2
        bool equal_key;    // If we find a equal key in this slot
        SlotInfo (uint32_t b, uint32_t a, int s, H1Tag h1, H2Tag h2, bool euqal, int os = 0)
            : bucket (b), cell (a), slot (s), old_slot (os), H1 (h1), H2 (h2), equal_key (euqal) {}
        SlotInfo ()
            : bucket (0), cell (0), slot (0), old_slot (0), H1 (0), H2 (0), equal_key (false) {}

        std::string ToString () {
            std::ostringstream ss;
            ss << "bi: " << bucket << ", ci: " << cell << ", si: " << slot << ", H2: " << H2
               << ", H1: " << H1;
            return ss.str ();
        }
    };  // end of class SlotInfo

    class CellAllocator {
    public:
        inline char* Allocate (size_t cell_count) {
            size_t size = cell_count * kCellSize;
            return static_cast<char*> (RP_malloc (size));
        }

        inline char* Calloc (size_t num, size_t size) {
            return static_cast<char*> (RP_calloc (num, size));
        }

        inline void Release (char* addr) { RP_free (addr); }
    };

    /** RecordAllocator
     *  @note: allocate memory space for new record
     */
    class RecordAllocator {
    public:
        inline char* Allocate (size_t size) { return reinterpret_cast<char*> (RP_malloc (size)); }

        inline void Release (char* addr) { RP_free ((void*)addr); }
    };

    template <typename T1, bool key_flat, bool value_flat>
    class DataRecord;

    template <typename T1>
    class DataRecord<T1, true, true> {
    public:
        DataRecord () = default;
        explicit DataRecord (const H1Tag& k, const Entry& v) : key_ (k), val_ (v) {}
        inline Key key () { return key_; }
        inline T value () { return val_; }

    private:
        Key key_;
        T val_;
    };

    /**
     * @brief both key and value is numeric type
     * HashSlot:
     *          | key | value |
     */
    template <typename T1>
    struct SlotRecord<T1, true, true> : public HashSlot {
        inline void Store (uint64_t hash, const Key& key, const T& value,
                           RecordAllocator& allocator) {
            HashSlot::H1 = key;
            HashSlot::entry = value;
            FLUSH (this);
            FLUSHFENCE;
        }

        inline char* ReleaseAddress () { return nullptr; }

        inline Key first (void) { return HashSlot::H1; }

        inline T second (void) { return HashSlot::entry; }

        inline Key compareKey (void) { return HashSlot::H1; }

        DataRecord<T1, true, true> Record () {
            return DataRecord<T1, true, true>{HashSlot::H1, HashSlot::entry};
        }
    };

    template <typename T1>
    class DataRecord<T1, true, false> {
    public:
        DataRecord () = default;
        explicit DataRecord (const H1Tag& k, const Entry& ptr) : key_ (k), ptr_ (ptr) {}
        inline Key key () { return key_; }
        inline T value () { return DecodeInRecord2<true, false, false, Key, T>::Decode (ptr_); }

    private:
        Key key_;
        char* ptr_;
    };

    /**
     * @brief key is numeric, value is std::string
     * HashSlot:
     *          | key | pointer | -> | key | val_len | value
     *                                     | size_t  | ...
     */
    template <typename T1>
    struct SlotRecord<T1, true, false> : public HashSlot {
        inline void Store (uint64_t hash, const Key& key, const T& value,
                           RecordAllocator& allocator) {
            size_t buf_len = Record2Format<true, false, Key, T>::Length (key, value);
            char* addr = (char*)allocator.Allocate (buf_len);
            EncodeToRecord2<true, false, Key, T>::Encode (key, value, addr);

            HashSlot::H1 = key;
            HashSlot::entry = addr;
            FLUSH (addr);
            FLUSH (this);
            FLUSHFENCE;
        }

        inline char* ReleaseAddress () { return HashSlot::entry; }

        inline Key first (void) { return HashSlot::H1; }

        inline T second (void) {
            return DecodeInRecord2<true, false, false, Key, T>::Decode (HashSlot::entry);
        }

        inline Key compareKey (void) { return HashSlot::H1; }

        DataRecord<T1, true, false> Record () {
            return DataRecord<T1, true, false>{HashSlot::H1, HashSlot::entry};
        }
    };

    template <typename T1>
    class DataRecord<T1, false, true> {
    public:
        DataRecord () = default;
        explicit DataRecord (const H1Tag& k, const Entry& kvptr) : h1_ (k), ptr_ (kvptr) {}
        inline Key key () { return DecodeInRecord2<false, true, true, Key, T>::Decode (ptr_); }
        inline T value () { return DecodeInRecord2<false, true, false, Key, T>::Decode (ptr_); }

    private:
        H1Tag h1_;
        char* ptr_;
    };
    /**
     * @brief key is std::string, value is numeric
     * HashSlot:
     *          | H1 | pointer | -> | key_len | value | key_buffer
     *                              | size_t  |   T   |  ...
     */
    template <typename T1>
    struct SlotRecord<T1, false, true> : public HashSlot {
        inline void Store (uint64_t hash, const Key& key, const T& value,
                           RecordAllocator& allocator) {
            size_t buf_len = Record2Format<false, true, Key, T>::Length (key, value);
            char* addr = (char*)allocator.Allocate (buf_len);
            EncodeToRecord2<false, true, Key, T>::Encode (key, value, addr);

            HashSlot::H1 = hash;
            HashSlot::entry = addr;
            FLUSH (addr);
            FLUSH (this);
            FLUSHFENCE;
        }

        inline char* ReleaseAddress () { return HashSlot::entry; }

        inline Key first (void) {
            return DecodeInRecord2<false, true, true, Key, T>::Decode (HashSlot::entry);
        }

        inline T second (void) {
            return DecodeInRecord2<false, true, false, Key, T>::Decode (HashSlot::entry);
        }

        inline util::Slice compareKey (void) {
            return DecodeInRecord2<false, true, true, Key, T>::Decode (HashSlot::entry);
        }

        DataRecord<T1, false, true> Record () {
            return DataRecord<T1, false, true>{HashSlot::H1, HashSlot::entry};
        }
    };

    template <typename T1>
    class DataRecord<T1, false, false> {
    public:
        DataRecord () = default;
        explicit DataRecord (const H1Tag& k, const Entry& kvptr) : h1_ (k), ptr_ (kvptr) {}
        inline Key key () { return DecodeInRecord2<false, false, true, Key, T>::Decode (ptr_); }
        inline T value () { return DecodeInRecord2<false, false, false, Key, T>::Decode (ptr_); }

    private:
        H1Tag h1_;
        char* ptr_;
    };
    /**
     * @brief key and value both are std::string
     * HashSlot:
     *          | H1 | pointer | -> | key_len | value_len | key_buf | value_buf
     *                              | size_t  |  size_t   |  ...    |  ...
     *
     */
    template <typename T1>
    struct SlotRecord<T1, false, false> : public HashSlot {
        inline void Store (uint64_t hash, const Key& key, const T& value,
                           RecordAllocator& allocator) {
            size_t buf_len = Record2Format<false, false, Key, T>::Length (key, value);
            char* addr = (char*)allocator.Allocate (buf_len);
            EncodeToRecord2<false, false, Key, T>::Encode (key, value, addr);

            HashSlot::H1 = hash;
            HashSlot::entry = addr;
            FLUSH (addr);
            FLUSH (this);
            FLUSHFENCE;
        }

        inline char* ReleaseAddress () { return HashSlot::entry; }

        inline Key first (void) {
            return DecodeInRecord2<false, false, true, Key, T>::Decode (HashSlot::entry);
        }

        inline T second (void) {
            return DecodeInRecord2<false, false, false, Key, T>::Decode (HashSlot::entry);
        }

        inline util::Slice compareKey (void) {
            return DecodeInRecord2<false, false, true, Key, T>::Decode (HashSlot::entry);
        }

        DataRecord<T1, false, false> Record () {
            return DataRecord<T1, false, false>{HashSlot::H1, HashSlot::entry};
        }
    };

    using RecordType = DataRecord<Key, is_key_flat, is_value_flat>;

    /** BucketMetaDram
     *  @note: a 8-byte
     */
    class BucketMetaDram {
    public:
        explicit BucketMetaDram (char* addr, uint32_t cell_count) {
            data_ = (((uint64_t)addr) << 16) | (__builtin_ctz (cell_count) << 8);
        }

        BucketMetaDram () : data_ (0) {}

        BucketMetaDram (const BucketMetaDram& a) = default;

        inline char* Address () { return (char*)(data_ >> 16); }

        inline uint32_t CellCountMask () { return CellCount () - 1; }

        inline uint32_t CellCount () { return (1 << ((data_ >> 8) & 0xFF)); }

        inline void Reset (char* addr, uint32_t cell_count) {
            data_ = (data_ & 0xFF) | (((uint64_t)addr) << 16) | (__builtin_ctz (cell_count) << 8);
        }

        inline bool TryLock (void) {
            return util::turbo_bit_spin_try_lock ((uint32_t*)(&data_), 0);
        }

        inline void Lock (void) { util::turbo_bit_spin_lock ((uint32_t*)(&data_), 0); }

        inline void Unlock (void) { util::turbo_bit_spin_unlock ((uint32_t*)(&data_), 0); }

        inline bool IsLocked (void) { return util::turbo_lockbusy ((uint32_t*)(&data_), 0); }

        inline bool TryRehashLock (void) {
            return util::turbo_bit_spin_try_lock ((uint32_t*)(&data_), 1);
        }

        inline void RehashLock (void) { util::turbo_bit_spin_lock ((uint32_t*)(&data_), 1); }

        inline void RehashUnlock (void) { util::turbo_bit_spin_unlock ((uint32_t*)(&data_), 1); }

        inline bool IsRehashLocked (void) { return util::turbo_lockbusy ((uint32_t*)(&data_), 1); }

        // LSB
        // | 1 b bucket lock | 1 b rehash lock | 6 b reserved | 8 b cell mask | 48 b address |
        uint64_t data_;
    };

    class BucketLockScope {
    public:
        BucketLockScope (BucketMetaDram* bucket_meta) : meta_ (bucket_meta) { meta_->Lock (); }

        ~BucketLockScope () { meta_->Unlock (); }
        BucketMetaDram* meta_;
    };

    class BucketMetaPmem {
    public:
        // Initialize the bucket meta in pmem
        void Init (BucketMetaDram bucket_meta) {
            cur_ver_ = 1;
            bucket1_ptr_ = bucket_meta.Address ();
            cell_count_leftshit_1_ = __builtin_ctz (bucket_meta.CellCount ());
            FLUSH (this);
            FLUSHFENCE;
        }

        // store the newest version of bucket meta and then commit
        void Store (BucketMetaDram bucket_meta) {
            if (cur_ver_ == 2) {
                // if current version is on 2, commit to 1
                bucket1_ptr_ = bucket_meta.Address ();
                cell_count_leftshit_1_ = __builtin_ctz (bucket_meta.CellCount ());
            } else if (cur_ver_ == 1) {
                // if current version if on 1, commit to 2
                bucket2_ptr_ = bucket_meta.Address ();
                cell_count_leftshit_2_ = __builtin_ctz (bucket_meta.CellCount ());
            } else {
                TURBO_PMEM_ERROR ("The version is wrong. cur_ver: " << cur_ver_);
                printf ("The version is wrong. cur_ver: %u\n", cur_ver_);
                exit (1);
            }
            FLUSH (this);
            FLUSHFENCE;

            // Switch the version
            cur_ver_ = 3 - cur_ver_;
            FLUSH (this);
            FLUSHFENCE;
        }

        // Obtain the right version of BucketMetaDram
        BucketMetaDram Extract (void) {
            BucketMetaDram bucket_meta;
            if (cur_ver_ == 1) {
                bucket_meta.Reset (bucket1_ptr_, 1 << cell_count_leftshit_1_);
                return bucket_meta;
            } else if (cur_ver_ == 2) {
                bucket_meta.Reset (bucket2_ptr_, 1 << cell_count_leftshit_2_);
                return bucket_meta;
            }
            TURBO_PMEM_ERROR ("The version is wrong. cur_ver: " << cur_ver_);
            exit (1);
        }

    private:
        BucketMetaPmem () {}

        uint8_t cur_ver_;
        uint8_t cell_count_leftshit_1_;
        uint8_t cell_count_leftshit_2_;
        pptr<char> bucket1_ptr_;
        pptr<char> bucket2_ptr_;
    };

    /** Usage: iterator every slot in the bucket, return the pointer in the slot
     *  BucketIterator<CellMeta256V2> iter(bucket_addr, cell_count_);
     *  while (iter.valid()) {
     *      ...
     *      ++iter;
     *  }
     */
    class BucketIterator {
    public:
        struct InfoPair {
            SlotInfo slot_info;
            HashSlot* hash_slot;
        };

        BucketIterator (uint32_t bi, char* bucket_addr, size_t cell_count, size_t cell_i = 0)
            : bi_ (bi),
              cell_count_ (cell_count),
              cell_i_ (cell_i),
              bitmap_ (0),
              bucket_addr_ (bucket_addr) {
            assert (bucket_addr != 0);
            CellMeta meta (bucket_addr);
            bitmap_ = meta.ValidBitSet ();
            if (!bitmap_) toNextValidBitMap ();
            // printf("Initial Bucket iter at ai: %u, si: %u\n", cell_i_, *bitmap_);
        }

        explicit operator bool () const { return (cell_i_ < cell_count_); }

        // ++iterator
        inline BucketIterator& operator++ () {
            ++bitmap_;
            if (!bitmap_) {
                toNextValidBitMap ();
            }
            return *this;
        }

        inline InfoPair operator* () const {
            // return the cell index, slot index and its slot content
            uint8_t slot_index = *bitmap_;
            char* cell_addr = bucket_addr_ + cell_i_ * CellMeta::CellSize ();
            HashSlot* slot = CellMeta::LocateSlot (cell_addr, slot_index);
            H2Tag H2 = *CellMeta::LocateH2Tag (cell_addr, slot_index);
            return {{bi_ /* ignore bucket index */, cell_i_ /* cell index */,
                     *bitmap_ /* slot index*/, static_cast<H1Tag> (slot->H1), H2, false, 0},
                    slot};
        }

        inline bool valid () { return cell_i_ < cell_count_ && (bitmap_ ? true : false); }

        std::string ToString () {
            char buffer[128];
            sprintf (buffer, "cell: %8d, slot: %2d", cell_i_, *bitmap_);
            return buffer;
        }

    private:
        inline void toNextValidBitMap () {
            while (!bitmap_ && cell_i_ < cell_count_) {
                cell_i_++;
                if (cell_i_ == cell_count_) return;
                char* cell_addr = bucket_addr_ + (cell_i_ << CellMeta::CellSizeLeftShift);
                CellMeta meta (cell_addr);
                bitmap_ = meta.ValidBitSet ();
            }
        }

        friend bool operator== (const BucketIterator& a, const BucketIterator& b) {
            return a.cell_i_ == b.cell_i_ && a.bitmap_ == b.bitmap_;
        }

        friend bool operator!= (const BucketIterator& a, const BucketIterator& b) {
            return a.cell_i_ != b.cell_i_ || a.bitmap_ != b.bitmap_;
        }

        uint32_t bi_;
        uint32_t cell_count_;
        uint32_t cell_i_;
        util::BitSet bitmap_;
        char* bucket_addr_;
    };

public:
    static constexpr int kSizeVecCount = 1 << 4;

    TurboHashTable () {
        // Do nothing
    }

    TurboHashTable (uint32_t bucket_count, uint32_t cell_count) {
        Initialize (bucket_count, cell_count);
    }

    ~TurboHashTable () { RP_close (); }

    struct TurboRoot {
        size_t bucket_count_;
        pptr<BucketMetaPmem> buckets_pmem_;
    };

    /**
     * @brief initialize the pmem hashtable. return the pointer of pmem bucket
     * meta list
     *
     * @param bucket_count
     * @param cell_count
     * @return pptr<BucketMetaPmem>
     */
    pptr<BucketMetaPmem> Initialize (uint32_t bucket_count, uint32_t cell_count) {
        // Step1. Initialize pmem library
        bool res = RP_init ("turbo_hash_pmem", TURBO_PMEM_LOG_SIZE);
        TurboRoot* turbo_root = nullptr;
        if (res) {
            TURBO_PMEM_INFO ("Prepare to recover");
            turbo_root = RP_get_root<TurboRoot> (0);
            int recover_res = RP_recover ();
            if (recover_res == 1) {
                TURBO_PMEM_INFO ("Dirty open, recover");
            } else {
                TURBO_PMEM_INFO ("Clean restart");
            }
        } else {
            TURBO_PMEM_INFO ("Clean create");
        }
        turbo_root = (TurboRoot*)RP_malloc (sizeof (TurboRoot));
        RP_set_root (turbo_root, 0);

        // Step2. Initialize members
        bucket_count_ = bucket_count;
        bucket_mask_ = bucket_count - 1;
        if (!util::isPowerOfTwo (bucket_count) || !util::isPowerOfTwo (cell_count)) {
            printf ("the hash table size setting is wrong. bucket: %u, cell: %u\n", bucket_count,
                    cell_count);
            exit (1);
        }

        // Step3. Allocate BucketMetaDram space
        size_t bucket_meta_space = bucket_count * sizeof (BucketMetaDram);
        BucketMetaDram* buckets_addr = (BucketMetaDram*)malloc (bucket_meta_space);
        if (buckets_addr == nullptr) {
            fprintf (stderr, "malloc %lu space fail.\n", bucket_meta_space);
            exit (1);
        }
        TURBO_PMEM_INFO ("BucketMetaDram Allocated: " << bucket_meta_space);
        memset ((char*)buckets_addr, 0, bucket_meta_space);
        buckets_ = buckets_addr;

        // Step4. Allocate BucketMetaPmem space
        BucketMetaPmem* buckets_pmem_addr =
            reinterpret_cast<BucketMetaPmem*> (RP_malloc (bucket_count * sizeof (BucketMetaPmem)));
        if (buckets_pmem_addr == nullptr) {
            fprintf (stderr, "malloc %lu space fail.\n", bucket_meta_space);
            exit (1);
        }
        TURBO_PMEM_INFO ("BucketMetaPmem Allocated: " << RP_malloc_size (buckets_pmem_addr));
        buckets_pmem_ = buckets_pmem_addr;

        // Step5. Allocate space of cells, then set the bucket meta (cell count and
        // pointer)
        for (size_t i = 0; i < bucket_count_; ++i) {
            // Allocate pmem space for cells
            uint32_t rnd_cell_count = cell_count;
            char* addr = cell_allocator_.Allocate (rnd_cell_count);

            // Reset the cell content to zero
            memset (addr, 0, rnd_cell_count * kCellSize);
            TURBO_PMEM_CLFLUSH (addr, rnd_cell_count * kCellSize);

            // Initialize the dram bucket meta
            buckets_[i].Reset (addr, rnd_cell_count);

            // Initialzie the pmem bucket meta
            buckets_pmem_[i].Init (buckets_[i]);
        }

        // Step6. Set the TurboRoot
        turbo_root->bucket_count_ = bucket_count_;
        turbo_root->buckets_pmem_ = buckets_pmem_;
        FLUSH (turbo_root);
        FLUSHFENCE;

        return buckets_pmem_addr;
    }

    void Recover () {
        // Step1. Open the pmem file
        bool res = RP_init ("turbo_hash_pmem", TURBO_PMEM_LOG_SIZE);
        if (res) {
            TURBO_PMEM_INFO ("Prepare to recover");
            RP_get_root<TurboRoot> (0);
            int recover_res = RP_recover ();
            if (recover_res == 1) {
                TURBO_PMEM_INFO ("Dirty open, recover");
            } else {
                TURBO_PMEM_INFO ("Clean restart");
            }
        } else {
            TURBO_PMEM_ERROR ("Nothing to recover");
            printf ("There is nothing to recover.");
            exit (1);
        }

        // Step2. Obtain the bucket_count and buckets meta from pmem
        TurboRoot* turbo_root = RP_get_root<TurboRoot> (0);
        bucket_count_ = turbo_root->bucket_count_;
        bucket_mask_ = bucket_count_ - 1;
        buckets_pmem_ = turbo_root->buckets_pmem_;
        buckets_ = (BucketMetaDram*)malloc (bucket_count_ * sizeof (BucketMetaDram));
        TURBO_PMEM_INFO ("Turbo reboot bucket count: " << bucket_count_);
        printf ("Turbo reboot bucket count: %lu\n", bucket_count_);

        // Step3. Recover the BucketMetaDram
        for (size_t i = 0; i < bucket_count_; ++i) {
            buckets_[i] = buckets_pmem_[i].Extract ();
            TURBO_PMEM_INFO ("Recovered bucket cell count: " << buckets_[i].CellCount ());
        }
        printf ("Finish recover\n");
    }

    double LoadFactor () {
        // TODO
        return 0.0;
    }

    size_t Capacity () {
        size_t sum = 0;
        for (size_t b = 0; b < bucket_count_; ++b) {
            sum += buckets_[b].CellCount () * (CellMeta::SlotCount () - 1);
        }
        return sum;
    }

    size_t Size () {
        // TODO
        return 0;
    }
    /** MinorReHashAll
     *  @note: not thread safe. This global rehashing will double the hash table
     * capacity.
     *  !
     */
    size_t MinorReHashAll () {
        // rehash for all the buckets
        int rehash_thread = 4;
        printf ("Rehash threads: %d\n", rehash_thread);
        std::vector<std::thread> workers (rehash_thread);
        std::atomic<size_t> rehash_count (0);
        auto rehash_start = util::NowMicros ();
        for (int t = 0; t < rehash_thread; t++) {
            workers[t] = std::thread ([&, t] {
                auto thread_info = getThreadInfo ();
                size_t start_b = bucket_count_ / rehash_thread * t;
                size_t end_b = start_b + bucket_count_ / rehash_thread;
                size_t counts = 0;
                for (size_t i = start_b; i < end_b; ++i) {
                    counts += MinorRehash (i, thread_info);
                }
                rehash_count.fetch_add (counts, std::memory_order_relaxed);
            });
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });
        double rehash_duration = util::NowMicros () - rehash_start;
        printf ("Real rehash speed: %f Mops/s. entries: %lu, duration: %.2f s.\n",
                (double)rehash_count.load () / rehash_duration, rehash_count.load (),
                rehash_duration / 1000000.0);
        return rehash_count.load ();
    }

    size_t GCAll () {
        // rehash for all the buckets
        int rehash_thread = 4;
        printf ("Rehash threads: %d\n", rehash_thread);
        std::vector<std::thread> workers (rehash_thread);
        std::atomic<size_t> rehash_count (0);
        auto rehash_start = util::NowMicros ();
        for (int t = 0; t < rehash_thread; t++) {
            workers[t] = std::thread ([&, t] {
                auto thread_info = getThreadInfo ();
                size_t start_b = bucket_count_ / rehash_thread * t;
                size_t end_b = start_b + bucket_count_ / rehash_thread;
                size_t counts = 0;
                for (size_t i = start_b; i < end_b; ++i) {
                    counts += MinorRehash (i, thread_info, true);
                }
                rehash_count.fetch_add (counts, std::memory_order_relaxed);
            });
        }
        std::for_each (workers.begin (), workers.end (), [] (std::thread& t) { t.join (); });
        double rehash_duration = util::NowMicros () - rehash_start;
        printf ("Real rehash speed: %f Mops/s. entries: %lu, duration: %.2f s.\n",
                (double)rehash_count.load () / rehash_duration, rehash_count.load (),
                rehash_duration / 1000000.0);
        return rehash_count.load ();
    }

    struct FindNextSlotInRehashResult {
        uint32_t cell_index;
        uint8_t slot_index;
    };

    // return the cell index and slot index
    inline FindNextSlotInRehashResult findNextSlotInRehash (uint8_t* slot_vec, H1Tag h1,
                                                            uint32_t cell_count_mask) {
        uint32_t ai = H1ToHash (h1) & cell_count_mask;
        int loop_count = 0;

        // find next cell that is not full yet
        uint32_t SLOT_MAX_RANGE = CellMeta::SlotMaxRange ();
        while (slot_vec[ai] >= SLOT_MAX_RANGE) {
            // because we use linear probe, if this cell is full, we go to next cell
            ai += ProbeWithinBucket::PROBE_STEP;
            loop_count++;
            if TURBO_PMEM_UNLIKELY (loop_count > ProbeWithinBucket::MAX_PROBE_LEN) {
                printf (
                    "ERROR!!! Even we rehash this bucket, we cannot find a valid "
                    "slot within %d "
                    "probe\n",
                    ProbeWithinBucket::MAX_PROBE_LEN);
                exit (1);
            }
            if (ai > cell_count_mask) {
                ai &= cell_count_mask;
            }
        }
        return {ai, slot_vec[ai]++};
    }

    template <typename E, bool is_flat_entry>
    struct PptrToDramPptr {};

    /**
     * @brief Covert pptr in ralloc space
     *
     * @tparam for flat entry, copy old_entry to des_entry_dram directly
     */
    template <typename E>
    struct PptrToDramPptr<E, true> {
        static inline void Convert (E& des_entry_dram, E& des_entry_pmem, HashSlot* old_slot_dram,
                                    HashSlot* old_slot_pmem) {
            des_entry_dram = old_slot_dram->entry;
        }
    };

    /**
     * @brief Covert pptr in ralloc space
     *
     * @tparam E is pptr<char> type, we need to calculate real off in
     * des_entry_pmem, then assign it to des_entry_dram
     */
    template <typename E>
    struct PptrToDramPptr<E, false> {
        static inline void Convert (E& des_entry_dram, E& des_entry_pmem, HashSlot* old_slot_dram,
                                    HashSlot* old_slot_pmem) {
            E* old_slot_entry_addr = (E*)&old_slot_pmem->entry;
            char* old_entry_addr = from_pptr_off (old_slot_dram->entry.off, old_slot_entry_addr);
            uint64_t off = to_pptr_off (old_entry_addr, &des_entry_pmem);
            des_entry_dram.off = off;
        }
    };

    /**
     * @brief Rehash bucket bi.
     *        This version copies the bucket to dram, then do rehashing on dram,
     * then copy back to pmem
     *
     * @param bi
     * @return size_t: amount of the rehashed iterms.
     */
    size_t MinorRehash (int bi, ThreadInfo& thread_info, bool isgc = false) {
        size_t count = 0;
        BucketMetaDram* bucket_meta = locateBucket (bi);

        // Step 1. Create new bucket and initialize its meta
        uint32_t old_cell_count = bucket_meta->CellCount ();
        size_t old_bucket_size = old_cell_count * kCellSize;
        char* old_bucket_addr_pmem = bucket_meta->Address ();
        char* old_bucket_addr_dram = (char*)aligned_alloc (kCellSize, old_bucket_size);
        memmove (old_bucket_addr_dram, old_bucket_addr_pmem, old_bucket_size);
        uint32_t new_cell_count = isgc ? old_cell_count : old_cell_count << 1;
        uint32_t new_cell_count_mask = new_cell_count - 1;
        size_t new_bucket_size = new_cell_count * kCellSize;
        char* new_bucket_addr_dram = (char*)aligned_alloc (kCellSize, new_bucket_size);
        char* new_bucket_addr_pmem = cell_allocator_.Allocate (new_cell_count);

        if (new_bucket_addr_dram == nullptr) {
            perror ("rehash alloc dram fail\n");
            exit (1);
        }
        if (new_bucket_addr_pmem == nullptr) {
            perror ("rehash alloc pmem fail\n");
            exit (1);
        }
        if (new_cell_count > kCellCountLimit) {
            printf ("Cannot rehash.\n");
            exit (1);
        }

        // Reset all cell's meta data
        for (size_t i = 0; i < new_cell_count; ++i) {
            char* des_cell_addr = new_bucket_addr_dram + (i << kCellSizeLeftShift);
            memset (des_cell_addr, 0, CellMeta::size ());
        }
        // ----------------------------------------------------------------------------------
        // iterator old bucket and insert slots info to new bucket
        // old: |11111111|22222222|33333333|44444444|
        //       ========>
        // new: |1111    |22222   |333     |4444    |1111    |222     |33333   |4444
        // |
        // ----------------------------------------------------------------------------------

        // Step 2. Move the meta in old bucket to new bucket
        //      a) Record next avaliable slot position of each cell within new
        //      bucket for rehash
        uint8_t* slot_vec = (uint8_t*)malloc (new_cell_count);
        memset (slot_vec, CellMeta::StartSlotPos (), new_cell_count);
        BucketIterator iter (bi, old_bucket_addr_dram, old_cell_count);
        //      b) Iterate every slot in this bucket
        while (iter.valid ()) {
            count++;
            // Step 1. obtain old slot info and slot content
            typename BucketIterator::InfoPair res = *iter;

            // Step 2. update bitmap, H2, H1 and slot pointer in new bucket
            //      a) find valid slot in new bucket
            FindNextSlotInRehashResult des_slot_info =
                findNextSlotInRehash (slot_vec, res.slot_info.H1, new_cell_count_mask);
            //      b) obtain des cell addr
            char* des_cell_addr_dram =
                new_bucket_addr_dram + (des_slot_info.cell_index << kCellSizeLeftShift);
            char* des_cell_addr_pmem =
                new_bucket_addr_pmem + (des_slot_info.cell_index << kCellSizeLeftShift);
            if (des_slot_info.slot_index >= CellMeta::SlotMaxRange ()) {
                printf ("rehash fail: %s\n", res.slot_info.ToString ().c_str ());
                TURBO_PMEM_ERROR ("Rehash fail: " << res.slot_info.ToString ());
                printf ("%s\n", PrintBucketMeta (res.slot_info.bucket).c_str ());
                exit (1);
            }
            //      c) move the old slot to new slot
            const SlotInfo& old_info = res.slot_info;
            HashSlot* old_slot_dram = res.hash_slot;
            char* old_cell_addr_pmem = old_bucket_addr_pmem + (old_info.cell << kCellSizeLeftShift);
            HashSlot* old_slot_pmem = CellMeta::LocateSlot (old_cell_addr_pmem, old_info.slot);

            // move slot content, including H1 and pointer
            HashSlot* des_slot_dram =
                CellMeta::LocateSlot (des_cell_addr_dram, des_slot_info.slot_index);
            HashSlot* des_slot_pmem =
                CellMeta::LocateSlot (des_cell_addr_pmem, des_slot_info.slot_index);
            PptrToDramPptr<Entry, is_key_flat && is_value_flat>::Convert (
                des_slot_dram->entry, des_slot_pmem->entry, old_slot_dram, old_slot_pmem);
            des_slot_dram->H1 = old_info.H1;

            // locate H2 and set H2
            H2Tag* h2_tag_ptr =
                CellMeta::LocateH2Tag (des_cell_addr_dram, des_slot_info.slot_index);
            *h2_tag_ptr = old_info.H2;

            // obtain and set bitmap
            decltype (CellMeta::Version::bitmap_)* bitmap =
                (decltype (CellMeta::Version::bitmap_)*)des_cell_addr_dram;
            *bitmap = (*bitmap) | (1 << des_slot_info.slot_index);

            // Step 3. to next old slot
            ++iter;
        }
        //      c) set remaining slots' slot pointer to 0 (including the backup
        //      slot)
        for (uint32_t ci = 0; ci < new_cell_count; ++ci) {
            char* des_cell_addr = new_bucket_addr_dram + (ci << kCellSizeLeftShift);
            for (uint8_t si = slot_vec[ci]; si <= CellMeta::SlotMaxRange (); si++) {
                HashSlot* des_slot = CellMeta::LocateSlot (des_cell_addr, si);
                des_slot->entry = 0;
                des_slot->H1 = 0;
            }
        }

        // Step 3. Copy content to pmem space
        memmove (new_bucket_addr_pmem, new_bucket_addr_dram, new_bucket_size);
        TURBO_PMEM_CLFLUSH (new_bucket_addr_pmem, new_bucket_size);

        // Step 4. Reset bucket meta in buckets_
        bucket_meta->Reset (new_bucket_addr_pmem, new_cell_count);

        // Step 5. commit the changed bucket meta to pmem meta. As long as this is
        // not finished, if crash happens, turbo hash will reboot from the last
        // state
        buckets_pmem_[bi].Store (*bucket_meta);

        // Step 6. Garbage collection for old_bucket
        epoche_.markNodeForDeletion ([=] () { cell_allocator_.Release (old_bucket_addr_pmem); },
                                     thread_info);

        // TURBO_PMEM_DEBUG("Rehash bucket: " << bi <<
        //      ", old cell count: " << old_cell_count <<
        //      ", loadfactor: " << (double)count / ( old_cell_count *
        //      (CellMeta::SlotCount() - 1) ) <<
        //      ", new cell count: " << new_cell_count);

        free (slot_vec);
        free (new_bucket_addr_dram);
        free (old_bucket_addr_dram);
        return count;
    }

    template <typename HashKey>
    inline size_t KeyToHash (HashKey& key) {
        using Mix = typename std::conditional<std::is_same<::turbo_pmem::hash<Key>, hasher>::value,
                                              ::turbo_pmem::identity_hash<size_t>,
                                              ::turbo_pmem::hash<size_t>>::type;
        return Mix{}(WHash::operator() (key));
    }

    template <typename K>
    struct IdenticalReturn {
        constexpr K operator() (K const& key) const noexcept { return key; }
    };

    // For CellMeta256V2, H1 may be used to store real key,
    // we need to calculate the real hash of h1 accordingly.
    // If key is flat (store the real key), we need to hash h1.
    inline size_t H1ToHash (H1Tag h1) {
        using ToHash = typename std::conditional<is_key_flat, ::turbo_pmem::hash<size_t>,
                                                 IdenticalReturn<H1Tag>>::type;
        return ToHash{}(h1);
    }

    inline ThreadInfo getThreadInfo () { return ThreadInfo (this->epoche_); }

    /** Put
     *  @note: insert or update a key-value record, return false if fails.
     */
    bool Put (const Key& key, const T& value, ThreadInfo& thread_info) {
        EpocheGuard epoche_guard (thread_info);
        // calculate hash value of the key
        size_t hash_value = KeyToHash (key);
        // update index, thread safe
        return insertSlot (key, value, hash_value, thread_info);
    }

    template <typename Fn>
    bool Find (const Key& key, ThreadInfo& thread_info, Fn&& callback) {
        EpocheGuardReadonly epoche_guard (thread_info);
        // calculate hash value of the key
        size_t hash_value = KeyToHash (key);
        FindSlotResult res = findSlot (key, hash_value);
        if (res.find) {
            callback (res.record);
            return true;
        }
        return false;
    }

    bool Delete (const Key& key, ThreadInfo& thread_info) {
        EpocheGuard epoche_guard (thread_info);
        // calculate hash value of the key
        size_t hash_value = KeyToHash (key);
        return deleteSlot (key, hash_value, thread_info);
    }

    void IterateValidBucket () {
        printf ("Iterate Valid Bucket\n");
        for (size_t i = 0; i < bucket_count_; ++i) {
            auto& bucket_meta = locateBucket (i);
            BucketIterator iter (i, bucket_meta.Address (), bucket_meta.info.cell_count);
            if (iter.valid ()) {
                printf ("%s\n", PrintBucketMeta (i).c_str ());
            }
        }
    }

    void IterateBucket (uint32_t i) {
        auto& bucket_meta = locateBucket (i);
        BucketIterator iter (i, bucket_meta.Address (), bucket_meta.CellCount ());
        while (iter.valid ()) {
            auto res = (*iter);
            SlotInfo& info = res.slot_info;
            HashSlot* slot = res.hash_slot;
            SlotType* record = reinterpret_cast<SlotType*> (slot);
            std::cout << info.ToString () << ", H1: " << slot->H1 << ". key: " << record->first ()
                      << ", value: " << record->second () << std::endl;
            ++iter;
        }
    }

    void IterateAll () {
        size_t count = 0;
        for (size_t i = 0; i < bucket_count_; ++i) {
            BucketMetaDram* bucket_meta = locateBucket (i);
            BucketIterator iter (i, bucket_meta->Address (), bucket_meta->CellCount ());
            while (iter.valid ()) {
                auto res = (*iter);
                SlotInfo& info = res.slot_info;
                HashSlot* slot = res.hash_slot;
                SlotType* record = reinterpret_cast<SlotType*> (slot);
                std::cout << info.ToString () << ", H1: " << slot->H1
                          << ". key: " << record->first () << ", value: " << record->second ()
                          << std::endl;
                ++iter;
                count++;
            }
        }
        printf ("iterato %lu entries. \n", count);
    }

    std::string ProbeStrategyName () { return ProbeWithinBucket::name (); }

    std::string PrintBucketMeta (uint32_t bucket_i) {
        std::string res;
        char buffer[1024];
        BucketMetaDram* bucket_meta = locateBucket (bucket_i);
        char* search_bucket_addr = bucket_meta->Address ();
        sprintf (buffer, "----- bucket %10u -----\n", bucket_i);
        res += buffer;
        ProbeWithinBucket probe (0, bucket_meta->CellCountMask (), bucket_i);
        uint32_t i = 0;
        int count_sum = 0;
        while (probe) {
            char* cell_addr = locateCell (search_bucket_addr, probe.offset ());
            CellMeta meta (cell_addr);
            int count = meta.OccupyCount ();
            sprintf (buffer, "\t%4u - 0x%12lx: %s. Cell valid slot count: %d. ", i++,
                     (uint64_t)cell_addr, meta.BitMapToString ().c_str (), count);
            res += buffer;
            auto valid_bitset = meta.ValidBitSet ();
            for (int i : valid_bitset) {
                std::ostringstream ss;
                HashSlot* slot = CellMeta::LocateSlot (cell_addr, i);
                ss << "s" << i << "H1: " << slot->H1 << ", ";
                res += ss.str ();
            }
            res += "\n";
            probe.next ();
            count_sum += count;
        }
        sprintf (buffer, "\tBucket %u: valid slot count: %d. Load factor: %f\n", bucket_i,
                 count_sum,
                 (double)count_sum / ((CellMeta::SlotCount () - 1) * bucket_meta->CellCount ()));
        res += buffer;
        return res;
    }

    void PrintAllMeta () {
        for (size_t b = 0; b < bucket_count_; ++b) {
            printf ("%s\n", PrintBucketMeta (b).c_str ());
        }
    }

    void PrintHashTable () {
        for (int b = 0; b < bucket_count_; ++b) {
            printf ("%s\n", PrintBucketMeta (b).c_str ());
        }
    }

private:
    inline uint32_t bucketIndex (uint64_t hash) { return hash & bucket_mask_; }

    inline BucketMetaDram* locateBucket (uint32_t bi) const { return &buckets_[bi]; }

    // offset.first: bucket index
    // offset.second: cell index
    inline char* locateCell (char* bucket_addr, const std::pair<size_t, size_t>& offset) {
        return bucket_addr +                           // locate the bucket
               (offset.second << kCellSizeLeftShift);  // locate the cell cell
    }

    /** insertToSlotAndGC
     *  @note: Reuse or recycle the space of target slot's old entry.
     *         Set bitmap, H2, H1, pointer.
     */
    inline void insertToSlotAndGC (size_t hash_value, const Key& key, const T& value,
                                   char* cell_addr, const SlotInfo& info, ThreadInfo& thread_info) {
        // the cell has been locked, only one writer can call this function

        // locate the target slot
        SlotType* slot = CellMeta::LocateSlot (cell_addr, info.slot);

        // set H2
        H2Tag* h2_tag_ptr = CellMeta::LocateH2Tag (cell_addr, info.slot);
        *h2_tag_ptr = info.H2;

        // store the key value to slot, contain sfence
        slot->Store (hash_value, key, value, record_allocator_);

        // obtain bitmap and set bitmap
        auto version = CellMeta::LoadVersion (cell_addr);

        if (true == info.equal_key) {
            // set the new slot, toggle the old slot (to 0)
            version.bitmap_ = (version.bitmap_ | (1 << info.slot)) ^ (1 << info.old_slot);

            // clean the delete_bitmap,
            version.bitmap_deleted_ &= ~(1 << info.slot);

            // Garbage collection for outdated slot
            SlotType* old_slot = CellMeta::LocateSlot (cell_addr, info.old_slot);
            char* old_addr = old_slot->ReleaseAddress ();
            if (old_addr != nullptr) {
                epoche_.markNodeForDeletion ([=] () { record_allocator_.Release (old_addr); },
                                             thread_info);
            }
        } else {
            // Insertion: set the new slot
            version.bitmap_ |= (1 << info.slot);
            // clean the delete_bitmap
            version.bitmap_deleted_ &= ~(1 << info.slot);
        }

        version.seq_no_++;

        TURBO_PMEM_COMPILER_FENCE ();
        CellMeta::StoreVersion (cell_addr, version);

        FLUSH (cell_addr);
        FLUSHFENCE;
    }

    inline bool insertSlot (const Key& key, const T& value, size_t hash_value,
                            ThreadInfo& thread_info) {
        // Obtain the partial hash
        PartialHash partial_hash (key, hash_value);

    after_rehash:
        BucketMetaDram* bucket_meta = locateBucket (bucketIndex (partial_hash.bucket_hash_));

#ifndef PIN_KEY_TO_THREAD
        // Obtain the bucket lock
        BucketLockScope meta_lock (bucket_meta);
#else
        // Check if the bucket is locked for rehashing. Wait entil is unlocked.
        while (bucket_meta->IsRehashLocked ()) {
            TURBO_PMEM_CPU_RELAX ();
        }
#endif

        FindSlotForInsertResult res = findSlotForInsert (key, partial_hash);

        // find a valid slot in target cell
        if (res.find) {
#ifndef PIN_KEY_TO_THREAD
            char* bucket_addr = bucket_meta->Address ();
            char* cell_addr =
                locateCell (bucket_addr, {res.target_slot.bucket, res.target_slot.cell});
            insertToSlotAndGC (hash_value, key, value, cell_addr, res.target_slot, thread_info);
            return true;
#else
            // Obtain the bucket lock
            BucketLockScope meta_lock (bucket_meta);
            // it is possible after obtain the bucket lock,
            // the bucket already be rehashed. we need to compare the old address in
            // res with current one
            char* bucket_addr = bucket_meta->Address ();
            if (bucket_addr != res.search_bucket_addr) {
                goto after_rehash;
            }

            char* cell_addr =
                locateCell (bucket_addr, {res.target_slot.bucket, res.target_slot.cell});

            CellMeta meta (cell_addr);  // obtain the meta part after lock

            if TURBO_PMEM_LIKELY (!meta.Occupy (res.target_slot.slot) ||
                                  meta.IsDeleted (res.target_slot.slot)) {
                // If the new slot from 'findSlotForInsert' is not occupied, insert
                // directly
                insertToSlotAndGC (hash_value, key, value, cell_addr, res.target_slot, thread_info);
                return true;
            } else if (res.target_slot.equal_key) {
                // If this is an update request and the backup slot is occupied,
                // it means the backup slot has changed in current cell. So we
                // update the slot location.
                util::BitSet backup_bitset = meta.BackupBitSet ();
                if TURBO_PMEM_UNLIKELY (!backup_bitset) {
                    printf ("Cannot update.\n");
                    exit (1);
                }

                res.target_slot.slot = *backup_bitset;
                insertToSlotAndGC (hash_value, key, value, cell_addr, res.target_slot, thread_info);
                return true;
            } else {
                // current new slot has been occupied by another concurrent thread.
                auto backup_bitset = meta.BackupBitSet ();
                auto erased_bitset = meta.EraseBitSet ();

                if (erased_bitset.validCount () != 0) {
                    res.target_slot.slot = *erased_bitset;
                    insertToSlotAndGC (hash_value, key, value, cell_addr, res.target_slot,
                                       thread_info);
                    return true;
                } else if (backup_bitset.validCount () >= 2) {
                    res.target_slot.slot = *backup_bitset;
                    insertToSlotAndGC (hash_value, key, value, cell_addr, res.target_slot,
                                       thread_info);
                    return true;
                }

                // Current cell has no empty slot for insertion, we retry
                // 'findSlotForInsert' This unlikely happens with all previous effort.
                TURBO_PMEM_INFO ("retry find slot in Bucket "
                                 << res.target_slot.bucket << ". Cell " << res.target_slot.cell
                                 << ". Slot " << res.target_slot.slot << ". Key: " << key);
                goto after_rehash;
            }
#endif
        } else {
            // cannot find a valid slot for insertion, rehash current bucket
            // then retry
#ifndef PIN_KEY_TO_THREAD
            MinorRehash (res.target_slot.bucket, thread_info);
#else
            // Obtain the Bucket rehash lock. Otherwise, other thread is already
            // rehashing.
            if (bucket_meta->TryRehashLock ()) {
                // Obtain the bucket lock, so other thread will not insert during
                // rehashing
                BucketLockScope meta_lock (bucket_meta);

                // minor rehash will change the address part of bucket_meta
                MinorRehash (res.target_slot.bucket, thread_info);
                bucket_meta->RehashUnlock ();
            }
#endif
            goto after_rehash;
        }

        return false;
    }

    template <typename T1, bool key_flat>
    struct SlotKeyEqual {};

    // For flat key, we can skip this because key is stored in H1
    template <typename T1>
    struct SlotKeyEqual<T1, true> {
        bool operator() (const Key& key, SlotType* record_ptr) { return true; }
    };

    template <typename T1>
    struct SlotKeyEqual<T1, false> : public WrapKeyEqual<KeyEqual> {
        bool operator() (const Key& key, SlotType* record_ptr) {
            return WKeyEqual::operator() (key, record_ptr->compareKey ());
        }
    };

    struct FindSlotForInsertResult {
        SlotInfo target_slot;
        char* search_bucket_addr;
        bool find;
    };

    /** findSlotForInsert
     *  @note:  Find a valid slot for insertion.
     *  @out:   first: the slot info that should insert the key
     *          second:  whether we can find a valid (empty or belong to the same
     * key) slot for insertion ! We cannot insert if the second is false.
     */
    inline FindSlotForInsertResult findSlotForInsert (const Key& key, PartialHash& partial_hash) {
        uint32_t bucket_i = bucketIndex (partial_hash.bucket_hash_);
        auto h2_hash_vec = CellMeta::SetHashVec (partial_hash.H2_);
        int64_t cell_to_insert = -1;
        uint8_t slot_to_insert = 0;
        BucketMetaDram* bucket_meta = locateBucket (bucket_i);
        char* search_bucket_addr = bucket_meta->Address ();

        ProbeWithinBucket probe (H1ToHash (partial_hash.H1_), bucket_meta->CellCountMask (),
                                 bucket_i);

        int probe_count = 0;  // limit probe times
        while (probe && (probe_count++ < ProbeWithinBucket::MAX_PROBE_LEN)) {
            // Go to target cell
            auto offset = probe.offset ();
            char* cell_addr = locateCell (search_bucket_addr, offset);

        find_for_insert_retry:
            CellMeta meta (cell_addr);
            for (int i : meta.MatchBitSet (h2_hash_vec)) {
                // locate the slot reference
                SlotType* slot = CellMeta::LocateSlot (cell_addr, i);
                if TURBO_PMEM_LIKELY (slot->H1 == partial_hash.H1_) {
                    if (SlotKeyEqual<Key, is_key_flat>{}(key, slot)) {
                        // This is an update request
                        util::BitSet backup_bitset = meta.BackupBitSet ();
                        auto version = CellMeta::LoadVersion (cell_addr);
                        auto old_version = meta.GetVersion ();
                        if (old_version.seq_no_ + 1 < version.seq_no_) {
                            goto find_for_insert_retry;
                        }
                        return {{
                                    offset.first,     /* bucket */
                                    offset.second,    /* cell */
                                    *backup_bitset,   /* new slot */
                                    partial_hash.H1_, /* H1 */
                                    partial_hash.H2_, /* H2 */
                                    true,             /* equal_key */
                                    i                 /* old slot */
                                },
                                search_bucket_addr,
                                true};
                    }
                }
            }

            // choose deleted position first
            auto erase_bitset = meta.EraseBitSet ();
            auto backup_bitset = meta.BackupBitSet ();
            if (erase_bitset.validCount () != 0) {
                cell_to_insert = offset.second;
                slot_to_insert = *erase_bitset;
            }

            // Reach to the search path end
            if (!meta.Full ()) {
                if (cell_to_insert == -1) {
                    cell_to_insert = offset.second;
                    slot_to_insert = *backup_bitset;
                }
                auto version = CellMeta::LoadVersion (cell_addr);
                auto old_version = meta.GetVersion ();
                if (old_version.seq_no_ + 1 < version.seq_no_) {
                    goto find_for_insert_retry;
                }
                // return an empty slot or deleted slot for new insertion
                return {{
                            bucket_i,                 /* bucket */
                            (uint32_t)cell_to_insert, /* cell */
                            slot_to_insert,           /* slot */
                            partial_hash.H1_,         /* H1 */
                            partial_hash.H2_,         /* H2 */
                            false                     /* equal_key */
                        },
                        search_bucket_addr,
                        true};
            }

            // probe the next cell in the same bucket
            probe.next ();
        }

        // We can insert to deleted slot
        if (cell_to_insert != -1) {
            return {{bucket_i, (uint32_t)cell_to_insert, slot_to_insert, partial_hash.H1_,
                     partial_hash.H2_, false},
                    search_bucket_addr,
                    true};
        }

        // only when all the probes fail and there is no empty slot
        // exists in this bucket.
        return {
            {bucket_i, 0, 0, partial_hash.H1_, partial_hash.H2_, false}, search_bucket_addr, false};
    }

    struct FindSlotResult {
        RecordType record;
        bool find;
    };

    // Based on version retry lock-free read.
    inline FindSlotResult findSlot (const Key& key, size_t hash_value) {
        PartialHash partial_hash (key, hash_value);
        uint32_t bucket_i = bucketIndex (partial_hash.bucket_hash_);
        auto h2_hash_vec = CellMeta::SetHashVec (partial_hash.H2_);
        BucketMetaDram* bucket_meta = locateBucket (bucket_i);
        char* search_bucket_addr = bucket_meta->Address ();
        ProbeWithinBucket probe (H1ToHash (partial_hash.H1_), bucket_meta->CellCountMask (),
                                 bucket_i);

        int probe_count = 0;  // limit probe times
        while (probe && (probe_count++ < ProbeWithinBucket::MAX_PROBE_LEN)) {
            auto offset = probe.offset ();
            char* cell_addr = locateCell (search_bucket_addr, offset);

        find_retry:
            CellMeta meta (cell_addr);
            for (int i : meta.MatchBitSet (h2_hash_vec)) {
                SlotType* slot = CellMeta::LocateSlot (cell_addr, i);  // locate the slot reference
                if TURBO_PMEM_LIKELY (slot->H1 == partial_hash.H1_) {
                    if (SlotKeyEqual<Key, is_key_flat>{}(key, slot)) {
                        RecordType record = slot->Record ();
                        auto version = CellMeta::LoadVersion (cell_addr);
                        auto old_version = meta.GetVersion ();
                        if (old_version.seq_no_ + 1 < version.seq_no_) {
                            // if version changed since last read, and the seq_no advances more than
                            // 1 (which means >= 2 writes), we retry.
                            goto find_retry;
                        }
                        return {record, true};
                    }
                }
            }

            // If this cell still has more than one empty slot, then it means the key
            // does't exist.
            if (!meta.Full ()) {
                return {{}, false};
            }

            probe.next ();
        }

        // after all the probe, no key exist
        return {{}, false};
    }

    inline bool deleteSlot (const Key& key, size_t hash_value, ThreadInfo& thread_info) {
        PartialHash partial_hash (key, hash_value);
        uint32_t bucket_i = bucketIndex (partial_hash.bucket_hash_);
        auto h2_hash_vec = CellMeta::SetHashVec (partial_hash.H2_);

    after_rehash:
        BucketMetaDram* bucket_meta = locateBucket (bucket_i);

#ifdef PIN_KEY_TO_THREAD
        while (bucket_meta->IsRehashLocked ()) {
            TURBO_PMEM_CPU_RELAX ();
        }
#endif
        char* search_bucket_addr = bucket_meta->Address ();

        ProbeWithinBucket probe (H1ToHash (partial_hash.H1_), bucket_meta->CellCountMask (),
                                 bucket_i);

        int probe_count = 0;  // limit probe times
        while (probe && (probe_count++ < ProbeWithinBucket::MAX_PROBE_LEN)) {
            auto offset = probe.offset ();
            char* cell_addr = locateCell (search_bucket_addr, offset);
        delete_retry:
            CellMeta meta (cell_addr);
            for (int i : meta.MatchBitSet (h2_hash_vec)) {
                SlotType* slot = CellMeta::LocateSlot (cell_addr, i);  // locate the slot reference
                if TURBO_PMEM_LIKELY (slot->H1 == partial_hash.H1_) {
                    // Obtain the bucket lock
                    BucketLockScope meta_lock (bucket_meta);

                    auto version = CellMeta::LoadVersion (cell_addr);
                    auto old_version = meta.GetVersion ();
                    if (old_version.seq_no_ + 1 < version.seq_no_) {
                        goto delete_retry;
                    }

                    if (SlotKeyEqual<Key, is_key_flat>{}(key, slot)) {
                        // If this key exsit, set the deleted bitmap

                        // it is possible after obtain the bucket lock,
                        // the bucket has already been rehashed. we need to compare the old
                        // address
                        char* bucket_addr = bucket_meta->Address ();
                        if (bucket_addr != search_bucket_addr) {
                            goto after_rehash;
                        }

                        // Garbage collection for deleted record
                        char* old_addr = slot->ReleaseAddress ();
                        if (old_addr != nullptr) {
                            epoche_.markNodeForDeletion (
                                [=] () { record_allocator_.Release (old_addr); }, thread_info);
                        }

                        version.bitmap_deleted_ |= (1 << i);
                        version.seq_no_++;
                        CellMeta::StoreVersion (cell_addr, version);
                        FLUSH (cell_addr);
                        FLUSHFENCE;
                        return true;
                    }
                }
            }

            // If this cell still has more than one empty slot, then it means the key
            // does't exist.
            if (!meta.Full ()) {
                return false;
            }

            probe.next ();
        }

        return false;
    }

private:
    CellAllocator cell_allocator_;
    RecordAllocator record_allocator_;
    BucketMetaDram* buckets_;
    BucketMetaPmem* buckets_pmem_;
    size_t bucket_count_ = 0;
    size_t bucket_mask_ = 0;

    Epoche epoche_{256};

    static constexpr int kCellSize = CellMeta::CellSize ();
    static constexpr int kCellSizeLeftShift = CellMeta::CellSizeLeftShift;
};

};  // namespace detail

// When using std::string for Key, the KeyEqual uses std::equal_to<util::Slice>
template <typename Key, typename T, typename Hash = hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
using unordered_map = detail::TurboHashTable<
    Key, T, Hash,
    typename std::conditional<std::is_same<Key, std::string>::value == false /* is numeric */,
                              KeyEqual, std::equal_to<util::Slice>>::type,
    32768>;
};  // end of namespace turbo_pmem

#endif
