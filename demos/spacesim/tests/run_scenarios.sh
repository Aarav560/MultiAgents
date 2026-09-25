#!/bin/sh
# run_scenarios.sh - scenario sweep: builds, runs every scenario, sweeps integrators and
# gravity solvers, checks exit codes and energy-drift thresholds, and writes sample outputs.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BIN=build/orbit
SWEEP_DIR=build/sweep
LOG=build/run_scenarios.log

pass=0
fail=0

note() { printf '%s\n' "$1"; }

fail_case() {
    fail=$((fail + 1))
    note "FAIL: $1"
}

pass_case() {
    pass=$((pass + 1))
    note "PASS: $1"
}

# threshold for a given integrator name; empty means "no drift check" (euler is exempt)
threshold_for() {
    case "$1" in
        euler) echo "" ;;
        symplectic) echo "1e-2" ;;
        leapfrog) echo "1e-4" ;;
        rk4) echo "1e-8" ;;
        yoshida) echo "1e-8" ;;
        *) echo "1e-1" ;;
    esac
}

# extracts the "energy drift=<val>" number from an orbit run's log file
extract_drift() {
    grep -o 'energy drift=[0-9.eE+-]*' "$1" | tail -1 | cut -d= -f2
}

# compares a drift value against a threshold using awk (portable, avoids bc dependency)
drift_below() {
    awk -v d="$1" -v t="$2" 'BEGIN { exit !(d < t) }'
}

note "building..."
make >"$LOG.build" 2>&1 || { cat "$LOG.build"; note "FAIL: make build"; exit 1; }
pass_case "make build"

mkdir -p "$SWEEP_DIR"

note "running every scenario ini..."
for ini in scenarios/*.ini; do
    name=$(basename "$ini" .ini)
    out="build/run_${name}.log"
    if "$BIN" --config "$ini" --steps 300 --quiet >"$out" 2>&1; then
        pass_case "scenario $name (exit 0)"
    else
        cat "$out"
        fail_case "scenario $name exited non-zero"
    fi
done

note "sweeping integrators on figure8..."
for k in euler symplectic leapfrog rk4 yoshida; do
    out="build/run_figure8_${k}.log"
    if "$BIN" --scenario figure8 --integrator "$k" --steps 300 --quiet >"$out" 2>&1; then
        pass_case "figure8 integrator=$k (exit 0)"
    else
        cat "$out"
        fail_case "figure8 integrator=$k exited non-zero"
        continue
    fi
    thr=$(threshold_for "$k")
    if [ -z "$thr" ]; then
        note "SKIP drift check for exempt integrator=$k"
        continue
    fi
    drift=$(extract_drift "$out")
    if [ -z "$drift" ]; then
        fail_case "figure8 integrator=$k: no energy drift reported"
        continue
    fi
    if drift_below "$drift" "$thr"; then
        pass_case "figure8 integrator=$k drift=$drift < $thr"
    else
        fail_case "figure8 integrator=$k drift=$drift >= threshold $thr"
    fi
done

note "sweeping gravity solvers on cluster..."
for g in direct bh; do
    out="build/run_cluster_${g}.log"
    if "$BIN" --scenario cluster --gravity "$g" --steps 300 --quiet >"$out" 2>&1; then
        pass_case "cluster gravity=$g (exit 0)"
    else
        cat "$out"
        fail_case "cluster gravity=$g exited non-zero"
        continue
    fi
    # cluster has real collisions (inelastic mergers), which legitimately remove energy;
    # the leapfrog integrator itself is not exempt, so use a loose merger-tolerant threshold.
    thr="1.5e-1"
    drift=$(extract_drift "$out")
    if [ -z "$drift" ]; then
        fail_case "cluster gravity=$g: no energy drift reported"
        continue
    fi
    if drift_below "$drift" "$thr"; then
        pass_case "cluster gravity=$g drift=$drift < $thr"
    else
        fail_case "cluster gravity=$g drift=$drift >= threshold $thr"
    fi
done

note "writing sample CSV and PPM frames into $SWEEP_DIR..."
out="build/run_sweep_outputs.log"
if "$BIN" --scenario figure8 --steps 300 --every 100 --quiet \
        --csv "$SWEEP_DIR/figure8.csv" --ppm "$SWEEP_DIR" >"$out" 2>&1; then
    pass_case "sweep output run (exit 0)"
else
    cat "$out"
    fail_case "sweep output run exited non-zero"
fi

if [ -s "$SWEEP_DIR/figure8.csv" ]; then
    pass_case "CSV written and non-empty: $SWEEP_DIR/figure8.csv"
else
    fail_case "CSV missing or empty: $SWEEP_DIR/figure8.csv"
fi

frame_count=0
for f in "$SWEEP_DIR"/frame_*.ppm; do
    [ -e "$f" ] || continue
    if [ -s "$f" ]; then
        frame_count=$((frame_count + 1))
    else
        fail_case "PPM frame empty: $f"
    fi
done
if [ "$frame_count" -ge 3 ]; then
    pass_case "$frame_count PPM frames written and non-empty"
else
    fail_case "expected at least 3 non-empty PPM frames, found $frame_count"
fi

note ""
note "run_scenarios: $pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0
