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

#ifndef MLVLIB_LOCK_TRAITS_HPP_
#define MLVLIB_LOCK_TRAITS_HPP_

// Common lock concepts, compile-time traits, and debug-assertion helpers.
// Keeps generic consumers decoupled from concrete lock types (SpinLock,
// NullLock, std::mutex, or any third-party BasicLockable).

#include <cassert>
#include <concepts>
#include <mutex>
#include <optional>
#include <string_view>

// Empty-member optimization for zero-sized lock policies (e.g. NullLock under
// NDEBUG). MSVC ignores standard [[no_unique_address]] and requires the
// vendor-prefixed spelling.
#ifndef MLVLIB_NO_UNIQUE_ADDRESS
#  if defined(_MSC_VER)
#    define MLVLIB_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#  else
#    define MLVLIB_NO_UNIQUE_ADDRESS [[no_unique_address]]
#  endif
#endif


namespace mlvlib
{

template<class L>
concept BasicLockable = requires(L& lock) {
    lock.lock();
    lock.unlock();
};

template<class L>
concept Lockable = BasicLockable<L> && requires(L& lock) {
    { lock.try_lock() } -> std::convertible_to<bool>;
};

// Optional debug introspection interface (provided in !NDEBUG builds by
// mlvlib locks; omitted in release and unavailable on std::mutex).
template<class L>
concept DebugIntrospectableLock = requires(const L& lock) {
    { lock.debug_is_locked() } -> std::convertible_to<bool>;
    { lock.debug_is_owned_by_current_thread() } -> std::convertible_to<bool>;
};


namespace detail
{

template<BasicLockable Lock>
struct lock_traits_base
{
    using lock_type  = Lock;
    using guard_type = std::scoped_lock<Lock>;

    static constexpr bool is_thread_safe          = true;
    static constexpr bool is_noop                 = false;
    static constexpr bool supports_try_lock       = Lockable<Lock>;
    static constexpr bool has_debug_introspection = DebugIntrospectableLock<Lock>;
    static constexpr std::string_view name        = "custom";

    // Returns std::nullopt when the lock cannot report ownership (e.g.
    // std::mutex or release builds where debug state is omitted).
    static constexpr std::optional<bool> debug_is_locked(const Lock& lock) noexcept
    {
        if constexpr (DebugIntrospectableLock<Lock>)
        {
            return lock.debug_is_locked();
        }
        else
        {
            (void)lock;
            return std::nullopt;
        }
    }

    static constexpr std::optional<bool>
    debug_is_owned_by_current_thread(const Lock& lock) noexcept
    {
        if constexpr (DebugIntrospectableLock<Lock>)
        {
            return lock.debug_is_owned_by_current_thread();
        }
        else
        {
            (void)lock;
            return std::nullopt;
        }
    }
};

} // namespace detail


// Primary template: any third-party BasicLockable gets safe defaults.
template<BasicLockable Lock>
struct lock_traits : detail::lock_traits_base<Lock>
{
};

// Explicit specialization for std::mutex (cannot add members to std::mutex).
template<>
struct lock_traits<std::mutex> : detail::lock_traits_base<std::mutex>
{
    static constexpr std::string_view name = "std::mutex";
};


// Debug precondition check for helpers that require the caller to hold `lock`.
// Active in !NDEBUG when Lock supports debug introspection; compiles to a no-op
// for std::mutex and in NDEBUG builds.
template<BasicLockable Lock>
constexpr void assert_lock_held([[maybe_unused]] const Lock& lock) noexcept
{
#ifndef NDEBUG
    if constexpr (lock_traits<Lock>::has_debug_introspection)
    {
        assert(lock.debug_is_owned_by_current_thread() &&
               "Precondition violated: lock must be held by current thread");
    }
#endif
}

} // namespace mlvlib

#endif // MLVLIB_LOCK_TRAITS_HPP_
