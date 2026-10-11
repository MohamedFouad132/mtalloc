// cross_thread_test.cpp: slots are allocated on one thread and freed on another.

#include "check.h"

#include <atomic>               // std::atomic
#include <condition_variable>   // std::condition_variable
#include <cstdint>              // uint64_t
#include <cstdio>               // printf
#include <cstdlib>              // malloc, free
#include <deque>                // std::deque
#include <mutex>                // std::mutex, std::unique_lock
#include <random>               // std::mt19937_64
#include <thread>               // std::thread
#include <vector>               // std::vector

constexpr int NUM_PRODUCERS = 8;
constexpr int NUM_CONSUMERS = 8;
constexpr uint64_t SLOTS_PER_PRODUCER = 250'000;
constexpr size_t MAX_QUEUED = 10'000;  // keeps producers from racing too far ahead

struct LiveSlot {
    void* ptr;
    size_t size;
    uint64_t tag;
};

// The queue between producers and consumers and everything that protects it
static std::deque<LiveSlot> queue;
static std::mutex queue_lock;
static std::condition_variable no_longer_empty;
static std::condition_variable no_longer_full;
static std::atomic<int> producers_done{0};
static std::atomic<uint64_t> consumed{0};


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


static void producer(uint64_t id) {
    std::mt19937_64 rng(id);

    for (uint64_t n = 0; n < SLOTS_PER_PRODUCER; n++) {
        LiveSlot slot;
        slot.size = random_size(rng);
        slot.ptr = malloc(slot.size);
        CHECK(slot.ptr != nullptr);
        slot.tag = (id << 48) | n;
        fill(slot.ptr, slot.size, slot.tag);

        std::unique_lock<std::mutex> guard(queue_lock);
        no_longer_full.wait(guard, [] { return queue.size() < MAX_QUEUED; });
        queue.push_back(slot);
        no_longer_empty.notify_one();
    }

    
    if (++producers_done == NUM_PRODUCERS) {
        std::lock_guard<std::mutex> guard(queue_lock); 
        no_longer_empty.notify_all();
    }
}

static void consumer() {
    while (true) {
        LiveSlot slot;
        {
            std::unique_lock<std::mutex> guard(queue_lock);
            no_longer_empty.wait(guard, [] { return !queue.empty() || producers_done == NUM_PRODUCERS; });
            if (queue.empty()) {
                return;  // every producer is finished and nothing is left
            }
            slot = queue.front();
            queue.pop_front();
            no_longer_full.notify_one();
        }

        CHECK(verify(slot.ptr, slot.size, slot.tag));
        free(slot.ptr);
        consumed++;
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        threads.emplace_back(producer, i);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        threads.emplace_back(consumer);
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    // Every slot a producer made must have been checked and freed by a consumer
    CHECK(consumed == NUM_PRODUCERS * SLOTS_PER_PRODUCER);

    std::printf("cross_thread_test: all checks passed\n");
    return 0;
}