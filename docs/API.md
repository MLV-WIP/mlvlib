# mlvlib API

Skeleton documentation. API detail will land when helpers, `SpinLock`, and
`LifetimeObserver` are ported from slab-pool-allocator (Phase 2).

## Headers

| Header | Contents (planned) |
|--------|--------------------|
| `mlvlib/helper.hpp` | Debug flags, print shims, `runtime_assert`, size literals |
| `mlvlib/spinlock.hpp` | Hybrid TTAS / backoff / block lock |
| `mlvlib/lifetimeobserver.hpp` | Intrusive weak liveness tracking |
| `mlvlib/mlvlib.hpp` | Umbrella include |

All public types and helpers live in namespace `mlvlib`.

## Deep dives

See [`docs/deepdive/`](deepdive/) for longer design notes (e.g. SpinLock analysis
after the port).
