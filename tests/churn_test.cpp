// churn_test.cpp: checks that slots return to central list after thread exit.

#include "check.h"

#include <cstdint>      // uint64_t
#include <cstdio>       // printf, fopen, fscanf, fclose
#include <cstdlib>      // malloc, free
#include <random>       // std::mt19937_64
#include <thread>       // std::thread
#include <unistd.h>     // sysconf
#include <vector>       // std::vector

constexpr int ROUNDS = 1000;
constexpr int THREADS_PER_ROUND = 8;
constexpr size_t SLOTS_PER_THREAD = 500;
constexpr int WARMUP_ROUNDS = 10;
constexpr size_t MAX_GROWTH = 64 * 1024 * 1024;  // 64 MiB

struct LiveSlot {
    void* ptr;
    size_t size;
    uint64_t tag;
};

// Physical memory this process is using right now.
static size_t resident_bytes() {
    FILE* file = std::fopen("/proc/self/statm", "r");
    CHECK(file != nullptr);

    unsigned long total_pages = 0;
    unsigned long resident_pages = 0;
    CHECK(std::fscanf(file, "%lu %lu", &total_pages, &resident_pages) == 2);
    std::fclose(file);

    return resident_pages * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}


static size_t small_size(std::mt19937_64& rng) {
    if (rng() % 100 < 90) {
        return 1 + rng() % 256;
    }
    return 257 + rng() % (8192 - 256);
}

static void short_lived(uint64_t thread_number) {
    std::mt19937_64 rng(thread_number);
    LiveSlot slots[SLOTS_PER_THREAD];

    for (size_t i = 0; i < SLOTS_PER_THREAD; i++) {
        slots[i].size = small_size(rng);
        slots[i].ptr = malloc(slots[i].size);
        CHECK(slots[i].ptr != nullptr);
        slots[i].tag = (thread_number << 16) | i;
        fill(slots[i].ptr, slots[i].size, slots[i].tag);
    }

    for (size_t i = 0; i < SLOTS_PER_THREAD; i++) {
        CHECK(verify(slots[i].ptr, slots[i].size, slots[i].tag));
        free(slots[i].ptr);
    }
}

int main() {
    size_t baseline = 0;

    for (int round = 0; round < ROUNDS; round++) {
        std::vector<std::thread> threads;
        for (int t = 0; t < THREADS_PER_ROUND; t++) {
            uint64_t thread_number = static_cast<uint64_t>(round) * THREADS_PER_ROUND + t;
            threads.emplace_back(short_lived, thread_number);
        }
        for (std::thread& thread : threads) {
            thread.join();
        }

        // Memory rises at first while the central lists fill up then should stabilise
        if (round == WARMUP_ROUNDS - 1) {
            baseline = resident_bytes();
        }

    }

    size_t final_bytes = resident_bytes();
    std::printf("churn_test: %zu KiB after warm-up, %zu KiB at the end\n", baseline / 1024, final_bytes / 1024);

    // gigabytes would be added if exiting threads were leaking.
    CHECK(final_bytes <= baseline + MAX_GROWTH);

    std::printf("churn_test: all checks passed\n");
    return 0;
}