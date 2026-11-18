# mlvlib

Header-only C++20+ utilities library — general helpers and primitives shared
across MLV projects (starting with [slab-pool-allocator](https://github.com/MLV-WIP/slab-pool-allocator)).

## Contents

| Header | Namespace | Description |
|--------|-----------|-------------|
| `mlvlib/helper.hpp` | `mlvlib` | Debug flags, `print`/`println` shims, `runtime_assert`, KiB/MiB/GiB literals |
| `mlvlib/spinlock.hpp` | `mlvlib` | Hybrid TTAS → backoff → `atomic_flag::wait` lock (`BasicLockable`) |
| `mlvlib/lifetimeobserver.hpp` | `mlvlib` | Intrusive weak liveness tracking for async callbacks |
| `mlvlib/mlvlib.hpp` | — | Umbrella include |

## Layout

```
mlvlib/                 # public headers (#include "mlvlib/...")
demos/                  # example programs
docs/                   # API notes
docs/deepdive/          # longer design write-ups (SpinLock analysis)
test/                   # Google Test suite
```

## Quick start

```bash
./configure --enable-tests --enable-demo
make
make test
make demo
```

**Build types** (same model as slab-pool-allocator):

```bash
make BUILD_TYPE=debug
make BUILD_TYPE=release
make BUILD_TYPE=addrsan     # default
make BUILD_TYPE=threadsan
make coverage               # release + gcov/llvm-cov summary
```

## Documentation

- [docs/API.md](docs/API.md) — API overview
- [docs/deepdive/SpinLock_Analysis.md](docs/deepdive/SpinLock_Analysis.md) — SpinLock correctness deep dive

## License

BSD 3-Clause. See [LICENSE](LICENSE).
