#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

LIB="$(pwd)/libmtalloc.so"

if [[ ! -f "$LIB" ]]; then
    echo "libmtalloc.so not found. Run make first"
    exit 1
fi

run() {
    local name=$1
    shift
    printf '  %-12s' "$name"
    if LD_PRELOAD="$LIB" "$@" > /dev/null; then
        echo "PASSED"
    else
        echo "FAILED"
        exit 1
    fi
}

PYTHON_WORKLOAD='
import threading, subprocess

def work():
    for i in range(100000):
        bytearray(i % 5000)

threads = [threading.Thread(target=work) for _ in range(8)]
for t in threads:
    t.start()
for t in threads:
    t.join()

assert subprocess.run(["echo", "hi"], capture_output=True).stdout == b"hi\n"
'

echo "real programs:"

run "ls"     ls -laR /usr/include
run "python" python3 -c "$PYTHON_WORKLOAD"
run "git"    git log --oneline -n 50
run "g++"    g++ -std=c++20 -O2 -c tests/api_test.cpp -o /tmp/mtalloc_real.o
run "sort"   bash -c "seq 1000000 | sort -R | sort -n"
run "grep"   grep -r "include" /usr/include
run "tar"    tar -czf /tmp/mtalloc_real.tar.gz -C /usr include