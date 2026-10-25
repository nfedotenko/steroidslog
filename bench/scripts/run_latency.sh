#!/usr/bin/env bash
# Runs the latency/throughput harness for every backend and collects CSVs.
#
# Published numbers must come from a quiet Linux box:
#   - performance governor (the script warns otherwise);
#   - producer pinned to a dedicated physical core (CPU_PROD), the process
#     restricted to CPU_PROD plus CPU_CONS so backend threads land on a
#     DIFFERENT physical core (pick non-SMT-siblings; see lscpu -e);
#   - describe the hardware next to the results.
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(realpath "$SCRIPT_DIR/../..")"

BUILD_DIR="${BUILD_DIR:-$ROOT/build-bench}"
OUT_DIR="${OUT_DIR:-$ROOT/bench_out}"
BIN="$BUILD_DIR/bench/steroidslog-benchmark"

CPU_PROD="${CPU_PROD:-2}"   # physical core for the producer
CPU_CONS="${CPU_CONS:-4}"   # physical core for backend/consumer threads
SAMPLES="${SAMPLES:-200000}"
WARMUP="${WARMUP:-20000}"
BACKENDS="${BACKENDS:-steroidslog spdlog quill fmtlog}"
THREAD_COUNTS="${THREAD_COUNTS:-1 4}"

mkdir -p "$OUT_DIR"

if [[ "$(uname -s)" == "Linux" ]]; then
  gov="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  if [[ "$gov" != "performance" ]]; then
    echo "WARNING: cpu governor is '$gov', not 'performance' — numbers are not publishable" >&2
  fi
fi

echo "Building..."
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSTEROIDSLOG_BUILD_BENCH=ON >/dev/null
cmake --build "$BUILD_DIR" -j >/dev/null

STAMP="$(date +%Y%m%d-%H%M%S)"
SUMMARY="$OUT_DIR/summary-$STAMP.csv"
RAW_DIR="$OUT_DIR/raw-$STAMP"
mkdir -p "$RAW_DIR"
echo "backend,case,threads,mode,samples,drops,p50,p90,p99,p99.9,p99.99,max,mean" > "$SUMMARY"

run() { # backend threads mode-args...
  local backend="$1" threads="$2"; shift 2
  local cmd=("$BIN" --backend "$backend" --samples "$SAMPLES" \
             --warmup "$WARMUP" --threads "$threads" \
             --summary-csv "$SUMMARY" "$@")
  if [[ "$(uname -s)" == "Linux" ]]; then
    # Producers get CPU_PROD..CPU_PROD+threads-1 (one core each), backend
    # threads end up on CPU_CONS — a different physical core.
    local last=$((CPU_PROD + threads - 1))
    local cpus
    cpus="$(seq -s, "$CPU_PROD" "$last"),$CPU_CONS"
    taskset -c "$cpus" "${cmd[@]}" --cpu-base "$CPU_PROD"
  else
    "${cmd[@]}"
  fi
}

for backend in $BACKENDS; do
  for t in $THREAD_COUNTS; do
    echo "== $backend, $t thread(s), latency =="
    run "$backend" "$t" --raw-dir "$RAW_DIR"
    echo "== $backend, $t thread(s), throughput =="
    run "$backend" "$t" --throughput
  done
done

echo
echo "Summary: $SUMMARY"
echo "Raw samples: $RAW_DIR"
