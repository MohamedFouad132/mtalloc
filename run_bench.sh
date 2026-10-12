#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"

RUNS=${RUNS:-5}
THREADS=(1 2 4 8 16)
ALLOCATORS=(glibc mtalloc nocache)
LIBS=("" "$(pwd)/libmtalloc.so" "$(pwd)/libmtalloc_nocache.so")
OUT=results/benchmark.csv

make
make libmtalloc_nocache.so
mkdir -p build results
g++ -std=c++20 -O2 -fno-builtin -Wall -Wextra -pthread -o build/bench bench/bench.cpp

echo "allocator,threads,run,ops_per_sec,lock_waits,peak_memory_kib" > "$OUT"

for ((run = 1; run <= RUNS; run++)); do
    for threads in "${THREADS[@]}"; do
        for i in "${!ALLOCATORS[@]}"; do
            allocator=${ALLOCATORS[$i]}
            printf '  run %d  %-8s %2d threads\n' "$run" "$allocator" "$threads"

            line=$(LD_PRELOAD="${LIBS[$i]}" ./build/bench "$threads")
            echo "$allocator,$threads,$run,${line#*,}" >> "$OUT"
        done
    done
done

echo "results written to $OUT"