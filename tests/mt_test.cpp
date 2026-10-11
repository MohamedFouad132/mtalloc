// mt_test.cpp: many threads allocating and freeing at once.

#include "check.h"

#include <cstdint>      // uint64_t
#include <cstdio>       // printf
#include <cstdlib>      // malloc, free
#include <random>       // std::mt19937_64
#include <thread>       // std::thread
#include <vector>       // std::vector

constexpr int NUM_THREADS = 8;
constexpr int OPS_PER_THREAD = 1'000'000;
constexpr size_t MAX_LIVE = 10'000;

struct LiveSlot {
    void* ptr;
    size_t size;
    uint64_t tag;
};


static size_t random_size(std::mt19937_64& rng) {
    uint64_t roll = rng() % 100; // pick a number between 0-99
    if (roll < 90) {
        return 1 + rng() % 256; // 1 - 256 bytes
    }
    if (roll < 99) {
        return 257 + rng() % (8192 - 256); // 257 - 8192 bytes
    }
    return 8193 + rng() % (65536 - 8192); // 8193 - 64KiB - 1
}

static void worker(uint64_t thread_id) {
    std::mt19937_64 rng(thread_id);  // fixed seed per thread.
    LiveSlot live[MAX_LIVE] = {};
    uint64_t next_tag = 0;

    for (int op = 0; op < OPS_PER_THREAD; op++) {
        LiveSlot& entry = live[rng() % MAX_LIVE];

        if (entry.ptr == nullptr) {
            entry.size = random_size(rng);
            entry.ptr = malloc(entry.size);
            CHECK(entry.ptr != nullptr);
            entry.tag = (thread_id << 48) | next_tag++;  // thread in the top bits keeps tags unique
            fill(entry.ptr, entry.size, entry.tag);
        } else {
            CHECK(verify(entry.ptr, entry.size, entry.tag));
            free(entry.ptr);
            entry.ptr = nullptr;
        }
    }

    // Check and free whatever is still alive
    for (LiveSlot& entry : live) {
        if (entry.ptr != nullptr) {
            CHECK(verify(entry.ptr, entry.size, entry.tag));
            free(entry.ptr);
        }
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(worker, i);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    std::printf("mt_test: all checks passed\n");
    return 0;
}