#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"

RUNS=${RUNS:-10}
LIB="$(pwd)/libmtalloc.so"
CXXFLAGS="-std=c++20 -O1 -fno-builtin -Wall -Wextra -pthread"
TESTS=(api_test mt_test cross_thread_test churn_test fork_test)

make
mkdir -p build

for test in "${TESTS[@]}"; do
    g++ $CXXFLAGS -o "build/$test" "tests/$test.cpp"
done

echo "tests:"

for test in "${TESTS[@]}"; do
    printf '  %-20s' "$test"
    for ((i = 1; i <= RUNS; i++)); do
        if ! LD_PRELOAD="$LIB" "./build/$test" > /dev/null; then
            echo "FAILED on run $i"
            exit 1
        fi
    done
    echo "PASSED"
done

echo
./tests/programs.sh

echo "all tests passed"