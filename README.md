# mlvlib

Header-only C++20+ utilities library — general helpers and primitives shared
across MLV projects (starting with [slab-pool-allocator](https://github.com/MLV-WIP/slab-pool-allocator)).

**Status:** skeleton. `SpinLock`, `LifetimeObserver`, and general helpers will be
ported here from spallocator; until then headers are placeholders plus a smoke
test.

## Layout

```
mlvlib/                 # public headers (#include "mlvlib/...")
demos/                  # example programs
docs/                   # API notes
docs/deepdive/          # longer design write-ups
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

## License

BSD 3-Clause. See [LICENSE](LICENSE).
