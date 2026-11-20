# mlvlib API

Unless noted, types and helpers live in namespace `mlvlib`.

## Headers

| Header | Contents |
|--------|----------|
| `mlvlib/helper.hpp` | Debug flags, print shims, `runtime_assert`, size literals |
| `mlvlib/spinlock.hpp` | `SpinLock` |
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

Debug builds record the owner thread id and abort on unlock-by-non-owner or
unlock-without-hold. Release builds are flag-only.

Deep dive: [SpinLock Analysis](deepdive/SpinLock_Analysis.md).

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
