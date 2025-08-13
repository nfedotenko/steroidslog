#!/usr/bin/env bash
set -Eeuo pipefail

### ──────────────────────────── pretty logging ────────────────────────────
c_reset=$'\e[0m'; c_dim=$'\e[2m'; c_bold=$'\e[1m'
c_green=$'\e[32m'; c_yellow=$'\e[33m'; c_blue=$'\e[34m'; c_red=$'\e[31m'

say()   { printf "%s[%s]%s %s\n" "$c_dim" "$(date +%H:%M:%S)" "$c_reset" "$*"; }
ok()    { printf "%s✔%s %s\n" "$c_green" "$c_reset" "$*"; }
warn()  { printf "%s⚠%s %s\n" "$c_yellow" "$c_reset" "$*"; }
fail()  { printf "%s✖%s %s\n" "$c_red" "$c_reset" "$*"; }

### ──────────────────────────── paths & options ───────────────────────────
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# repo root (prefer Git, fall back relative)
if git -C "$SCRIPT_DIR" rev-parse --show-toplevel >/dev/null 2>&1; then
  ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
else
  ROOT="$(realpath "$SCRIPT_DIR/../..")"
fi

BUILD_DIR="${BUILD_DIR:-$ROOT/build-bench}"
OUT_DIR="${OUT_DIR:-$ROOT/bench_out}"
BIN_NAME="${BIN_NAME:-steroidslog-benchmark}"

# CPUs to pin (producer threads + backend)
CPUSET_1T="${CPUSET_1T:-0-1}"   # one producer + backend
CPUSET_4T="${CPUSET_4T:-0-4}"   # four producers + backend

MIN_TIME="${MIN_TIME:-2.0}"
REPS="${REPS:-10}"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

### ──────────────────────────── build ─────────────────────────────────────
say "Configuring & building in: $BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DSTEROIDSLOG_BUILD_BENCH=ON -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON
cmake --build "$BUILD_DIR" -j

# find binary (either at root of build or under subdir)
if [[ -x "$BUILD_DIR/$BIN_NAME" ]]; then
  BIN="$BUILD_DIR/$BIN_NAME"
else
  # common alt: project subdir
  mapfile -t candidates < <(find "$BUILD_DIR" -maxdepth 3 -type f -name "$BIN_NAME" -perm -111 | sort)
  if [[ ${#candidates[@]} -gt 0 ]]; then BIN="${candidates[0]}"; else
    fail "Benchmark binary '$BIN_NAME' not found in $BUILD_DIR"; exit 1
  fi
fi
ok "Binary: $BIN"

### ──────────────────────────── CPU tuning (best effort) ──────────────────
needsudo() { [[ ${EUID:-$(id -u)} -ne 0 ]]; }
can_sudo() { sudo -n true 2>/dev/null; }

say "Applying optional CPU tuning (governor=performance, turbo off)"
if needsudo && ! can_sudo; then
  warn "No sudo without password; skipping CPU tuning (safe to ignore)."
else
  if needsudo; then pref='sudo -n'; else pref=''; fi

  # Set governor to performance for all cores
  if $pref bash -c 'echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor' 2>/dev/null; then
    for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
      $pref bash -c 'echo performance > "'"$f"'"' || true
    done
    ok "CPU governor set to performance (best effort)."  # linux cpufreq sysfs, governors
  else
    warn "Could not set governor; system may not expose cpufreq sysfs."
  fi

  # Disable turbo: Intel uses intel_pstate no_turbo=1; AMD often exposes cpufreq/boost=0
  vendor="$(LC_ALL=C lscpu | awk -F: '/Vendor ID/{gsub(/^[ \t]+/,"",$2);print $2}')"
  if [[ "$vendor" == *"GenuineIntel"* ]]; then
    if [[ -e /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
      $pref bash -c 'echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo' && ok "Intel turbo disabled (no_turbo=1)." || warn "Failed to toggle intel_pstate no_turbo."
      # 1 disables turbo per kernel docs
    else
      warn "intel_pstate no_turbo not present."
    fi
  elif [[ "$vendor" == *"AuthenticAMD"* ]]; then
    if [[ -e /sys/devices/system/cpu/cpufreq/boost ]]; then
      $pref bash -c 'echo 0 > /sys/devices/system/cpu/cpufreq/boost' && ok "AMD boost disabled (boost=0)." || warn "Failed to toggle cpufreq boost."
      # cpufreq boost sysfs
    else
      warn "cpufreq boost control not present."
    fi
  else
    warn "Unknown CPU vendor: $vendor"
  fi

  # Drop FS caches (optional, harmless for our file-less runs)
  $pref bash -c 'sync; echo 3 > /proc/sys/vm/drop_caches' && ok "Dropped filesystem caches." || true
fi

### ──────────────────────────── run (one pass) ────────────────────────────
say "Running benchmarks (ensure BOTH 1-thread and 4-thread registrations are compiled in)"
JSON_ALL="$OUT_DIR/figure5_all.json"
LOG_FILE="$OUT_DIR/bench.log"
: > "$LOG_FILE"

# For single-thread and 4-thread runs we don't need two executions:
# run once and let Python split by the 'threads' field.
say "  taskset (all): CPUs $CPUSET_1T"
taskset -c "$CPUSET_1T" "$BIN" \
  --benchmark_min_time="$MIN_TIME" \
  --benchmark_repetitions="$REPS" \
  --benchmark_report_aggregates_only=true \
  --benchmark_out="$JSON_ALL" \
  --benchmark_out_format=json \
  >> "$LOG_FILE" 2>&1 || { fail "Benchmark run failed (1T stage). See $LOG_FILE"; exit 1; }

ok "Raw JSON: $JSON_ALL"

### ──────────────────────────── plot (both threads) ───────────────────────
say "Rendering plots (threads=1 and threads=4)"
PY="$ROOT/bench/scripts/plot_figure5.py"
python3 "$PY" --json "$JSON_ALL" --threads 1 --out "$OUT_DIR/figure5_1t.png"
python3 "$PY" --json "$JSON_ALL" --threads 4 --out "$OUT_DIR/figure5_4t.png"

ok "Done."
say "Images:"
say "  1 Thread => $OUT_DIR/figure5_1t.png"
say "  4 Threads => $OUT_DIR/figure5_4t.png"
say "Log: $LOG_FILE"
