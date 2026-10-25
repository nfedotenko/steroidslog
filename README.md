# steroidslog

[![ci](https://github.com/nfedotenko/steroidslog/actions/workflows/ci.yml/badge.svg)](https://github.com/nfedotenko/steroidslog/actions/workflows/ci.yml)&nbsp;
[![Release](https://img.shields.io/github/release/nfedotenko/steroidslog.svg)](https://github.com/nfedotenko/steroidslog/releases/latest)

Steroidslog is a tiny, high‑throughput C++20 logger built for ultra-low latency workloads. The hot path is lock‑free and allocation‑free: a per‑thread SPSC queue records the *format id + raw arguments* in a compact layout and hands them off to a dedicated logger thread which performs the actual formatting and I/O.

## Why another logger?

- **Hot‑path cost matters.** In low‑latency systems you pay for every instruction on your producer threads. Steroidslog eliminates formatting and syscalls from the producing thread, pushing them to a single consumer.
- **Predictable performance.** Per‑thread ring buffers avoid cross‑core contention; queues are cache‑line padded and branch‑light to keep tail latency tight.
- **fmt‑style ergonomics.** You write logs with familiar `{}` formatting, but the work happens off the hot path.

## What makes it fast

- **Compile‑time format lookup.** Each log site bakes a `fmt_id` (constexpr hash/registration) so producers only push a small id + arguments. The full format string lives with the consumer.
- **Per‑thread SPSC rings.** Each thread has a bounded SPSC queue to the logger thread. No CAS storms, no shared MPMC; only release/acquire pairs on enqueue/dequeue.
- **Argument shipping, not string building.** Producers store arguments in a fixed set of slots (`uint64_t`/`double`/`string_view`), so *no allocations and almost no copies* on the hot path.
- **Batching at the sink.** The consumer thread formats in batches and flushes to the sink (file/console) to amortize syscalls.

## Performance

The published comparison against spdlog/quill/fmtlog is temporarily withdrawn: the logger core
is being reworked (correctness fixes, producer-side timestamps, overflow policies), and the old
numbers measured mean call time rather than latency percentiles, with competitor configurations
that need revisiting. A fair, reproducible benchmark suite — per-call latency histograms
(p50/p99/p99.9), identical sinks, drop counts for every logger, raw data and scripts in the
repo — will land together with the rework.

## Usage

```cpp
#include <steroidslog/steroidslog.h>

int main() {
    STERLOG_INFO("This is a static string.");
    STERLOG_INFO("order id={} price={}", 42, 123.45);
    STERLOG_DEBUG("vector size={} mean={:.3f}", 100, 0.12345);
    STERLOG_WARN("slow op: {} ms", 22.8);
    STERLOG_ERROR("bad state: msg={}", "some error msg");

    return 0;
}
```

> [!TIP]
> See `example/` for a compact starter (build it with `-O3 -march=native` for best results).

## Install

### Build this repo locally

```bash
git clone https://github.com/nfedotenko/steroidslog.git
cd steroidslog
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Optional:
```bash
# Build benchmarks and example
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTEROIDSLOG_BUILD_BENCH=ON -DSTEROIDSLOG_BUILD_EXAMPLE=ON
cmake --build build/bench -j
```

### Use in another project (CMake)

#### Option A — `FetchContent`

```cmake
include(FetchContent)
FetchContent_Declare(
  steroidslog
  GIT_REPOSITORY https://github.com/nfedotenko/steroidslog.git
  GIT_TAG main
)
FetchContent_MakeAvailable(steroidslog)
target_link_libraries(your_app PRIVATE steroidslog)
```

#### Option B — `add_subdirectory`

```cmake
add_subdirectory(externals/steroidslog) # path you cloned to
target_link_libraries(your_app PRIVATE steroidslog)
```

#### Option C — Single header *(coming soon)*

Copy `steroidslog.h` into your project and include it directly. This is ideal for quick experiments and minimal dependencies.

> [!TIP]
> For best performance, prefer the full build which enables LTO and per-target optimization flags.

## Supported platforms

- **Linux**: GCC (tested 13.3.0 & via CI) and Clang (via CI) on `ubuntu-latest`.
- **Windows**: MSVC (Visual Studio on `windows-latest`) via GitHub Actions.

## Roadmap / TODO

- [x] Compile‑time log‑level filtering
- [ ] Timestamp
- [ ] Different `std::ostream` targets
- [ ] Customizations:
  - [ ] Colored log
  - [ ] Blocking enqueue
  - [ ] Single‑threaded mode
  - [ ] Complex formatter (stuff like `{:.3f}`)
- [ ] Small hot‑path tweaks
- [ ] Single-header version

---

## License

MIT License — see [LICENSE](LICENSE) for details.
