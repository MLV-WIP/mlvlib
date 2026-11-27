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

#ifndef MLVLIB_NULL_LOCK_HPP_
#define MLVLIB_NULL_LOCK_HPP_

// No-op Lockable primitive for single-threaded / externally serialized builds.
//
// In release (NDEBUG) builds, NullLock is an empty class (zero bytes with
// MLVLIB_NO_UNIQUE_ADDRESS) whose lock/try_lock/unlock methods compile away
// completely. In debug (!NDEBUG) builds, NullLock records the owning thread id
// to catch accidental re-entrancy, double-unlock, and unlock-by-non-owner, and
// exposes debug introspection matching SpinLock.
//
// NullLock provides NO mutual exclusion and intentionally emits NO TSan
// happens-before annotations.

#ifndef NDEBUG
#  include <cassert>
#  include <thread>
#endif

#include "lock_traits.hpp"


namespace mlvlib
{

class NullLock
{
public:
    NullLock() = default;
    ~NullLock() = default;

    void lock() noexcept;
    bool try_lock() noexcept;
    void unlock() noexcept;

#ifndef NDEBUG
    bool debug_is_locked() const noexcept;
    bool debug_is_owned_by_current_thread() const noexcept;
#endif

private: // methods
    NullLock(const NullLock&) = delete;
    NullLock& operator=(const NullLock&) = delete;
    NullLock(NullLock&&) = delete;
    NullLock& operator=(NullLock&&) = delete;

#ifndef NDEBUG
private: // data members
    // Debug-only owner tracking; omitted in NDEBUG so NullLock stays empty.
    std::thread::id owner_{};
#endif
};


inline void NullLock::lock() noexcept
{
#ifndef NDEBUG
    assert(owner_ == std::thread::id{} &&
           "NullLock::lock called while already held");
    owner_ = std::this_thread::get_id();
#endif
}


inline bool NullLock::try_lock() noexcept
{
#ifndef NDEBUG
    if (owner_ != std::thread::id{})
    {
        return false;
    }
    owner_ = std::this_thread::get_id();
#endif
    return true;
}


inline void NullLock::unlock() noexcept
{
#ifndef NDEBUG
    assert(owner_ == std::this_thread::get_id() &&
           "NullLock::unlock by non-owner or while not held");
    owner_ = std::thread::id{};
#endif
}


#ifndef NDEBUG
inline bool NullLock::debug_is_locked() const noexcept
{
    return owner_ != std::thread::id{};
}


inline bool NullLock::debug_is_owned_by_current_thread() const noexcept
{
    return owner_ == std::this_thread::get_id();
}
#endif


template<>
struct lock_traits<NullLock> : detail::lock_traits_base<NullLock>
{
    static constexpr bool is_thread_safe   = false;
    static constexpr bool is_noop          = true;
    static constexpr std::string_view name = "null";
};

} // namespace mlvlib

#endif // MLVLIB_NULL_LOCK_HPP_
