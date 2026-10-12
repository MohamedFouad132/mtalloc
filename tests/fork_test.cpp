// fork_test.cpp: forks repeatedly while other threads allocate.

#include "check.h"

#include <atomic>       // std::atomic
#include <cstdint>      // uint64_t
#include <cstdio>       // printf
#include <cstdlib>      // malloc, free
#include <random>       // std::mt19937_64
#include <sys/wait.h>   // waitpid, WIFEXITED, WEXITSTATUS
#include <thread>       // std::thread
#include <unistd.h>     // fork, alarm, _exit
#include <vector>       // std::vector

constexpr int BACKGROUND_THREADS = 4;
constexpr int FORKS = 200;
constexpr size_t SLOTS_PER_SIZE = 70;          // more than one class's cache can hold
constexpr unsigned CHILD_TIMEOUT_SECONDS = 5;

static std::atomic<bool> stop{false};

// Mallocs and frees nonstop so class locks are constantly held.
static void background(uint64_t id) {
    std::mt19937_64 rng(id);
    void* live[1000] = {};

    while (!stop) {
        void*& entry = live[rng() % 1000];
        if (entry == nullptr) {
            entry = malloc(1 + rng() % 8192);
            CHECK(entry != nullptr);
        } else {
            free(entry);
            entry = nullptr;
        }
    }

    for (void* ptr : live) {
        free(ptr);
    }
}


[[noreturn]] static void child_work() {
    alarm(CHILD_TIMEOUT_SECONDS);  // the alarm kills the child process if a lock is stuck.


    for (size_t size = 16; size <= 8192; size += 16) {
        void* slots[SLOTS_PER_SIZE];
        for (void*& slot : slots) {
            slot = malloc(size);
            CHECK(slot != nullptr);
        }
        for (void* slot : slots) {
            free(slot);
        }
    }

    _exit(0);
}

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < BACKGROUND_THREADS; i++) {
        threads.emplace_back(background, i);
    }

    for (int i = 0; i < FORKS; i++) {
        pid_t pid = fork();
        CHECK(pid >= 0);

        if (pid == 0) {
            child_work();  // the child never returns from this
        }

        
        int status = 0;
        CHECK(waitpid(pid, &status, 0) == pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    stop = true;
    for (std::thread& thread : threads) {
        thread.join();
    }

    std::printf("fork_test: all checks passed\n");
    return 0;
}