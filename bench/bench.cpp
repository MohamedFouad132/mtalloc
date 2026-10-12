// bench.cpp: measures malloc and free throughput with a given number of threads

#include <atomic>       // std::atomic
#include <chrono>       // std::chrono::steady_clock
#include <cstdint>      // uint64_t
#include <cstdio>       // printf, fprintf
#include <cstdlib>      // malloc, free, atoi
#include <thread>       // std::thread
#include <vector>       // std::vector

#include <sys/resource.h>  // getrusage

constexpr uint64_t OPS_PER_THREAD = 10'000'000;
constexpr uint64_t WARMUP_OPS = 100'000;
constexpr size_t MAX_LIVE = 1000;
constexpr size_t MAX_SIZE = 1024;

static std::atomic<int> ready{0};
static std::atomic<bool> go{false};

// Very fast xorshift random number generator.
struct Rng {
    uint64_t state;

    uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
};

static void run_ops(Rng& rng, void** live, uint64_t ops) {
    for (uint64_t i = 0; i < ops; i++) {
        uint64_t random = rng.next();
        void*& entry = live[random % MAX_LIVE];

        if (entry == nullptr) {
            entry = malloc(1 + (random >> 32) % MAX_SIZE);
            static_cast<char*>(entry)[0] = 1;
        } else {
            free(entry);
            entry = nullptr;
        }
    }
}

static void worker(uint64_t id) {
    // Golden ratio constant spreads small thread ids into very different starting states
    Rng rng{(id + 1) * 0x9E3779B97F4A7C15};
    void* live[MAX_LIVE] = {};

    // Warm up runs
    run_ops(rng, live, WARMUP_OPS);

    ready++;
    while (!go) {
    }

    run_ops(rng, live, OPS_PER_THREAD);

    for (void* ptr : live) {
        free(ptr);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bench <threads>\n");
        return 1;
    }
    int num_threads = std::atoi(argv[1]);

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker, i);
    }

    while (ready < num_threads) {
    }

    // save process's context switch count and peak memory
    rusage before{};
    getrusage(RUSAGE_SELF, &before);

    auto start = std::chrono::steady_clock::now();
    go = true;

    for (std::thread& thread : threads) {
        thread.join();
    }
    auto end = std::chrono::steady_clock::now();
    
    // Check again
    rusage after{};
    getrusage(RUSAGE_SELF, &after);

    double seconds = std::chrono::duration<double>(end - start).count();
    double ops_per_sec = num_threads * OPS_PER_THREAD / seconds;
    long lock_waits = after.ru_nvcsw - before.ru_nvcsw;
    long peak_memory_kib = after.ru_maxrss;

    std::printf("%d,%.0f,%ld,%ld\n", num_threads, ops_per_sec, lock_waits, peak_memory_kib);
    return 0;
}