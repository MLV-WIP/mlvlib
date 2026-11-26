# mlvlib API

Unless noted, types and helpers live in namespace `mlvlib`.

## Headers

| Header | Contents |
|--------|----------|
| `mlvlib/helper.hpp` | Debug flags, print shims, `runtime_assert`, size literals |
| `mlvlib/lock_traits.hpp` | Lock concepts (`BasicLockable`, `Lockable`, `DebugIntrospectableLock`), `lock_traits<Lock>`, `assert_lock_held()`, `MLVLIB_NO_UNIQUE_ADDRESS` |
| `mlvlib/spinlock.hpp` | `SpinLock` + `lock_traits<SpinLock>` |
| `mlvlib/null_lock.hpp` | `NullLock` (no-op lock for single-threaded builds) + `lock_traits<NullLock>` |
| `mlvlib/lifetimeobserver.hpp` | `LifetimeObserver` |
| `mlvlib/mlvlib.hpp` | Umbrella include |

Include style: `#include "mlvlib/spinlock.hpp"` (with `-I` pointing at the
repo root, or install prefix `include/`).

---

## Helpers

**Header:** `mlvlib/helper.hpp`

| Symbol | Description |
|--------|-------------|
| `DEBUG_BUILD` / `VERBOSE_DEBUG` | Compile-time flags (`NDEBUG` / `VDEBUG`) |
| `operator""_KB` / `_MB` / `_GB` | Binary (1024-based) size literals |
| `print` / `println` | C++23 `std::print` when available; else `std::format` shim |
| `debug_print` / `debug_println` | Macros: no-op (args not evaluated) unless `VDEBUG` and not `NDEBUG`; otherwise forward to typed impls. Call unqualified — not `mlvlib::debug_print(...)` |
| `runtime_assert` | Debug-only abort with `source_location`; no-op in release |

---

## SpinLock

**Header:** `mlvlib/spinlock.hpp`

Lightweight mutex for **short** critical sections. Models `BasicLockable`
(`std::scoped_lock` / `std::unique_lock`). Non-copyable, non-movable.

Internally: uncontended `lock()` is a single acquire TAS; on failure,
`lock_contended()` does TTAS spin, per-thread staggered sleep backoff
(low bits of hashed `thread::id`, then +7 ns per retry; no RNG), then
`std::atomic_flag::wait`. Under ThreadSanitizer, acquire/release annotations
publish happens-before edges.

```cpp
namespace mlvlib {
class SpinLock {
public:
    void lock();
    bool try_lock();
    void unlock();
};
}
```

| Method | Description |
|--------|-------------|
| `lock()` | Blocks until acquired (spin → backoff → wait) |
| `try_lock()` | Non-blocking; `true` if acquired |
| `unlock()` | Release and wake one waiter; must be held by caller |

Debug builds record the owner thread id (using a relaxed `std::atomic<std::thread::id>`)
and abort on unlock-by-non-owner or unlock-without-hold. Debug builds also expose
TSan-clean introspection methods:

| Method (`!NDEBUG` only) | Description |
|-------------------------|-------------|
| `debug_is_locked()` | `true` if the lock flag is currently set |
| `debug_is_owned_by_current_thread()` | `true` if `owner_` matches `std::this_thread::get_id()` |

Release builds omit `owner_` and these methods (flag-only `SpinLock`).

Deep dive: [SpinLock Analysis](deepdive/SpinLock_Analysis.md).

---

## NullLock

**Header:** `mlvlib/null_lock.hpp`

No-op `Lockable` primitive for single-threaded or externally serialized builds.
Non-copyable, non-movable. Provides **no** mutual exclusion and intentionally
emits no ThreadSanitizer happens-before annotations.

```cpp
namespace mlvlib {
class NullLock {
public:
    void lock() noexcept;
    bool try_lock() noexcept;
    void unlock() noexcept;
#ifndef NDEBUG
    bool debug_is_locked() const noexcept;
    bool debug_is_owned_by_current_thread() const noexcept;
#endif
};
}
```

- **Release (`NDEBUG`):** empty class (`std::is_empty_v<NullLock> == true`, 0 bytes
  with `MLVLIB_NO_UNIQUE_ADDRESS`). `lock()` / `unlock()` are empty; `try_lock()`
  returns `true`.
- **Debug (`!NDEBUG`):** records the owning `std::thread::id` to abort on
  re-entrant `lock()`, unlock-without-hold, or unlock-by-non-owner; `try_lock()`
  returns `false` if already held; exposes `debug_is_locked()` and
  `debug_is_owned_by_current_thread()`.

---

## lock_traits

**Header:** `mlvlib/lock_traits.hpp`

Compile-time concepts and traits decoupling generic consumers from concrete lock
types (`SpinLock`, `NullLock`, `std::mutex`, or any third-party `BasicLockable`).

| Symbol | Description |
|--------|-------------|
| `BasicLockable<L>` | Concept requiring `lock()` and `unlock()` |
| `Lockable<L>` | `BasicLockable<L>` + `try_lock()` convertible to `bool` |
| `DebugIntrospectableLock<L>` | Concept detecting `!NDEBUG` `debug_is_locked()` / `debug_is_owned_by_current_thread()` |
| `lock_traits<Lock>` | Exposes `lock_type`, `guard_type` (`std::scoped_lock<Lock>`), `is_thread_safe`, `is_noop`, `supports_try_lock`, `has_debug_introspection`, `name`, and `std::optional<bool>` debug query forwarders (`std::nullopt` when unsupported) |
| `assert_lock_held(lock)` | Debug-only precondition assert when `has_debug_introspection` is `true`; no-op for `std::mutex` and in `NDEBUG` |
| `MLVLIB_NO_UNIQUE_ADDRESS` | Portable `[[no_unique_address]]` / `[[msvc::no_unique_address]]` macro |

| Type | `is_thread_safe` | `is_noop` | `supports_try_lock` | `name` |
|------|------------------|-----------|---------------------|--------|
| `mlvlib::SpinLock` | `true` | `false` | `true` | `"spinlock"` |
| `std::mutex` | `true` | `false` | `true` | `"std::mutex"` |
| `mlvlib::NullLock` | `false` | `true` | `true` | `"null"` |
| custom `BasicLockable` | `true` | `false` | `Lockable<L>` | `"custom"` |

---

## LifetimeObserver

**Header:** `mlvlib/lifetimeobserver.hpp`

Intrusive “is this object still alive?” helper for callbacks. Inspired by
`shared_ptr` / `weak_ptr`: **owner** refs keep the control-block owner count;
`getObserver()` returns a weak view that does not keep the object alive.

Typical pattern: derive from `LifetimeObserver`, capture `getObserver()` in a
callback, then call `isAlive()` before touching the object.

| Concern | Guarantee |
|---------|-----------|
| Control-block counts | `std::atomic`; concurrent `isAlive()` / ref adjust do not data-race |
| Same-thread / single-strand | If callback and destroy cannot overlap, `isAlive()`-then-use is complete |
| Cross-thread use + destroy | Serialize with the same mutex/strand that covers destroy |

```cpp
namespace mlvlib {
class LifetimeObserver {
public:
    enum class e_refType { owner, observer };
    bool isAlive() const;
    operator bool() const;
    LifetimeObserver getObserver() const;
    int64_t getCount(e_refType ref_type) const;
    // ...
};
}
```

See `demos/demo_lifetime_observer.cpp` for single-strand and mutex patterns.
