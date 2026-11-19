/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025-2026, Michael VanLoon
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MLVLIB_SPINLOCK_HPP_
#define MLVLIB_SPINLOCK_HPP_

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <functional>
#include <thread>


// ThreadSanitizer annotations for custom synchronization primitives
// These tell TSan about happens-before relationships in our SpinLock.
// GCC defines __SANITIZE_THREAD__; Clang uses __has_feature(thread_sanitizer).
#if defined(__SANITIZE_THREAD__)
  #define TSAN_ENABLED 1
#elif defined(__has_feature)
  #if __has_feature(thread_sanitizer)
    #define TSAN_ENABLED 1
  #endif
#endif

#ifdef TSAN_ENABLED
extern "C" {
    void __tsan_acquire(void *addr);
    void __tsan_release(void *addr);
}
#define TSAN_ANNOTATE_HAPPENS_BEFORE(addr) __tsan_release(addr)
#define TSAN_ANNOTATE_HAPPENS_AFTER(addr)  __tsan_acquire(addr)
#else
#define TSAN_ANNOTATE_HAPPENS_BEFORE(addr)
#define TSAN_ANNOTATE_HAPPENS_AFTER(addr)
#endif


namespace mlvlib
{

class SpinLock
{
public:
    SpinLock() = default;
    ~SpinLock() = default;

    void lock();
    bool try_lock();
    void unlock();

private: // methods
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    // Debug-only owner tracking: catch unlock-by-non-owner / double-unlock.
    // Release builds omit these members and checks (flag-only SpinLock).
    void mark_locked();
    void mark_unlocked();

private: // data members
    std::atomic_flag lock_flag = ATOMIC_FLAG_INIT;
#ifndef NDEBUG
    std::thread::id owner_{};
#endif
};


inline void SpinLock::lock()
{
    using namespace std::chrono_literals;

    // Must live across loop iterations; resetting inside the loop
    // would make the wait() fallback unreachable.
    int backoff_count = 0;
    auto wait_time = 0ns;

    // Test-and-Test-and-set (TTAS) spin lock with escalating backoff
    while (true)
    {
        // First, spin while the lock appears to be held
        for (int i = 0; i < 100; ++i)
        {
            if (!lock_flag.test(std::memory_order_relaxed))
            {
                // Lock appears free, try to acquire
                break;
            }
            std::this_thread::yield();
        }

        // Then, attempt to acquire the lock
        if (!lock_flag.test_and_set(std::memory_order_acquire))
        {
            TSAN_ANNOTATE_HAPPENS_AFTER(this);
            mark_locked();
            return; // Lock acquired
        }

        // Contended path only: per-thread staggered base backoff (no RNG).
        // thread::id is opaque, so hash then take the low 4 bits; +1 → 1..16 ns.
        // Same thread always gets the same base; each backoff loop adds 7 ns.
        if (backoff_count == 0)
        {
            thread_local static const auto kBaseBackoff = [] {
                using namespace std::chrono_literals;
                const std::size_t h =
                    std::hash<std::thread::id>{}(std::this_thread::get_id());
                return (1 + (h & 0xfu)) * 1ns;
            }();
            wait_time = kBaseBackoff;
        }

        // Failed to acquire - linear escalate then block
        if (backoff_count < 10)
        {
            std::this_thread::sleep_for(wait_time);
            wait_time += 7ns;
            ++backoff_count;
        }
        else
        {
            // After 10 attempts, use efficient blocking
            lock_flag.wait(true, std::memory_order_relaxed);
            // Loop back to try acquiring again
        }
    }
}


inline bool SpinLock::try_lock()
{
    if (lock_flag.test(std::memory_order_relaxed))
    {
        return false; // Lock is already held elsewhere
    }

    // Attempt to acquire the lock without blocking
    if (!lock_flag.test_and_set(std::memory_order_acquire))
    {
        TSAN_ANNOTATE_HAPPENS_AFTER(this);
        mark_locked();
        return true; // Lock acquired
    }
    // Lock not acquired; return immediately
    return false;
}


inline void SpinLock::unlock()
{
    mark_unlocked();
    TSAN_ANNOTATE_HAPPENS_BEFORE(this);
    lock_flag.clear(std::memory_order_release);
    lock_flag.notify_one();
}


inline void SpinLock::mark_locked()
{
#ifndef NDEBUG
    assert(owner_ == std::thread::id{} &&
           "SpinLock acquired while owner id still set");
    owner_ = std::this_thread::get_id();
#endif
}


inline void SpinLock::mark_unlocked()
{
#ifndef NDEBUG
    assert(owner_ == std::this_thread::get_id() &&
           "SpinLock::unlock by non-owner or while not held");
    owner_ = std::thread::id{};
#endif
}


} // namespace mlvlib

#endif // MLVLIB_SPINLOCK_HPP_
