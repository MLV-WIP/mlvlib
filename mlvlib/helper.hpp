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

#ifndef MLVLIB_HELPER_HPP_
#define MLVLIB_HELPER_HPP_

// General helpers: debug flags, print/println shims, runtime_assert,
// and binary size literals (KiB/MiB/GiB).

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <version>
#include <format>
#include <source_location>

// std::print / std::println are C++23. Prefer the library when the feature-test
// macro says it is present; otherwise provide a std::format-based shim below.
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
#  include <print>
#endif

namespace mlvlib
{

    #ifndef NDEBUG
        constexpr bool DEBUG_BUILD = true;
    #else
        constexpr bool DEBUG_BUILD = false;
    #endif

    #ifdef VDEBUG
        constexpr bool VERBOSE_DEBUG = DEBUG_BUILD;
    #else
        constexpr bool VERBOSE_DEBUG = false;
    #endif


    // User-defined literals for binary prefixes (powers of 1024)
    constexpr std::size_t operator""_KB(unsigned long long value) {
        return value * 1024;
    }

    constexpr std::size_t operator""_MB(unsigned long long value) {
        return value * 1024 * 1024;
    }

    constexpr std::size_t operator""_GB(unsigned long long value) {
        return value * 1024 * 1024 * 1024;
    }



    // format is required (C++20 library completeness; see configure).
    // print is optional (C++23); gate on __cpp_lib_print, not __cpp_lib_format.
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
    using std::print;
    using std::println;
#else
    // Adapted from GitHub Jhuighuy /TitSolver/source/tit/core/io_utils.hpp (MIT)
    // --- start Jhuighuy ---
    template<typename... Args>
    inline void print(std::format_string<Args...> fmt, Args&&... args)
    {
        std::cout << std::format(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void println(std::format_string<Args...> fmt, Args&&... args)
    {
        std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }
    // --- end Jhuighuy ---
#endif


    // Verbose debug printers (implementation). Prefer the debug_print /
    // debug_println macros below the namespace — a function-body
    // `if constexpr (VERBOSE_DEBUG)` still evaluates call-site arguments
    // (e.g. expensive printHex(...)), which showed up hot in allocator profiles.
    template<typename... Args>
    inline void debug_print_impl(std::format_string<Args...> fmt, Args&&... args)
    {
        print(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    inline void debug_println_impl(std::format_string<Args...> fmt, Args&&... args)
    {
        println(fmt, std::forward<Args>(args)...);
    }


    //
    // Runtime assertions are valuable because they are self-diagnosing code.
    //
    // What they are:
    // Checks that validate assumptions made by the program at runtime. If an
    // assumption is violated, the program can immediately halt and report
    // the error, rather than continuing in an invalid state which could lead
    // to more obscure bugs later on.
    //
    // What they are not:
    // They are not a substitute for proper error handling in production code.
    // They are primarily a debugging aid to catch programming errors during
    // development.
    // They should only be used in places where assumptions are being
    // validated that should reasonably never be violated if the code is
    // correct. They should never replace runtime checks for invalid states
    // that could occur due to external factors (e.g., invalid API
    // parameters, invalid user input, resource exhaustion, etc.)
    // They should never be used for operations with side-effects, as that
    // side-effect will evaporate in release builds.
    //
    // See also: std::static_assert for compile-time assertions.
    //
    inline void runtime_assert(bool condition,
                               const char* message,
                               const std::source_location& loc = std::source_location::current())
    {
        if constexpr (DEBUG_BUILD)
        {
            if (!condition)
            {
                std::cerr << std::format(
                    "Runtime assertion failed: {}\n  File: {}:{}\n  Function: {}\n",
                    message, loc.file_name(), loc.line(), loc.function_name());
                std::abort();
            }
        }
    }

    inline void runtime_assert(bool condition,
                               const std::string& message,
                               const std::source_location& loc = std::source_location::current())
    {
        runtime_assert(condition, message.c_str(), loc);
    }

} // namespace mlvlib


// Argument-eliding wrappers. When VERBOSE_DEBUG is false, these expand to a
// no-op and do not evaluate __VA_ARGS__. When true, they forward to the
// typed impls above. Call as debug_print(...) / debug_println(...) (not
// mlvlib::debug_print — a qualified name would leave a stray `mlvlib::`
// before a no-op expansion).
#if defined(VDEBUG) && !defined(NDEBUG)
#  define debug_print(...) ::mlvlib::debug_print_impl(__VA_ARGS__)
#  define debug_println(...) ::mlvlib::debug_println_impl(__VA_ARGS__)
#else
#  define debug_print(...) static_cast<void>(0)
#  define debug_println(...) static_cast<void>(0)
#endif


#endif // MLVLIB_HELPER_HPP_
