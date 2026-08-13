#!/usr/bin/env bash

set -u

OUTPUT_DIR=/tmp/kit-log-bench
mkdir -p "$OUTPUT_DIR"

sudo bpftrace \
    -d \
    tests/benchmarks/log/log_filter_futex_wait.bt \
    >"$OUTPUT_DIR/log-filter-futex-wait-dry-run.txt" \
    2>"$OUTPUT_DIR/log-filter-futex-wait-dry-run.err"

bpftrace_exit=$?
echo "bpftrace exit=$bpftrace_exit"

tail -20 \
    "$OUTPUT_DIR/log-filter-futex-wait-dry-run.txt"

cat \
    "$OUTPUT_DIR/log-filter-futex-wait-dry-run.err"

exit "$bpftrace_exit"