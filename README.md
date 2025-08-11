# steroidslog

Steroidslog is a tiny, high‑throughput C++20 logger built for ultra-low latency workloads. The hot path is lock‑free and allocation‑free: a per‑thread SPSC queue records the *format id + raw arguments* in a compact layout and hands them off to a dedicated logger thread which performs the actual formatting and I/O.

<p align="center">
  <img src="docs/figure5_1t.jpg" alt="Figure 5 style (1 thread)" width="70%"/>
  <br/>
  <em>Single-thread microbenchmarks (higher is better)</em>
</p>

<p align="center">
  <img src="docs/figure5_4t.jpg" alt="Figure 5 style (4 threads)" width="70%"/>
  <br/>
  <em>4-thread microbenchmarks (higher is better)</em>
</p>

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

Numbers below come from the included microbenchmarks (Figure 5‑style tests). On my machine (16c @ ~3.97GHz, Ubuntu 24.04, gcc‑13 -O3), steroidslog reaches **~210M logs/sec (1 thread)** on the simple tests and **~160–195M logs/sec** on more complex formatting. In the same setup, `fmtlog` is around **~106–108M**, `spdlog` around **~10–30M**, and `quill` around **~18–31M** depending on the case. See the plots above.

> [!CAUTION]
> Benchmarks are microbenchmarks; real apps will vary with I/O, sinks, and contention patterns.
> Reproduce locally before drawing conclusions.

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

## Supported platforms

- **Linux**: tested on **GCC 13.3.0** (Ubuntu 24.04).  
- Clang and MSVC should work; please open an issue with your results.

## Roadmap / TODO

- [ ] Compile‑time log‑level filtering
- [ ] Timestamp
- [ ] Different `std::ostream` targets
- [ ] Customizations:
  - [ ] Colored log
  - [ ] Blocking enqueue
  - [ ] Single‑threaded mode
- [ ] Small hot‑path tweaks (e.g., no‑args fast path)

---

## License

MIT License — see [LICENSE](LICENSE) for details.
