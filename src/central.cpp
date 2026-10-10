#include "shared.h"

#include <bit>          // std::has_single_bit
#include <mutex>        // std::mutex, std::lock_guard
#include <pthread.h>    // pthread_atfork

namespace mtalloc {

static FreeSlot* central_lists[NUM_CLASSES];
static std::mutex class_locks[NUM_CLASSES];  // one lock per class


// called when the list is empty for a specific class
static bool populate_central_list(size_t class_index) {
    char* chunk = static_cast<char*>(get_os_memory(CHUNK_SIZE));
    if (chunk == nullptr) {
        return false;
    }

    write_header(chunk, class_index, CHUNK_SIZE);

    size_t slot_size = SIZE_CLASSES[class_index];
    // align power of 2 slots naturally 
    size_t offset = std::max(class_alignment(class_index), sizeof(ChunkHeader));
    char* start = chunk + offset;
    char* end = chunk + CHUNK_SIZE;
    size_t number_of_slots = static_cast<size_t>(end - start) / slot_size;

    char* curr = start;
    for (size_t i = 0; i < number_of_slots; i++) {
        char* next = curr + slot_size;
        if (i + 1 < number_of_slots) {
            reinterpret_cast<FreeSlot*>(curr)->next = reinterpret_cast<FreeSlot*>(next);
        } else {
            reinterpret_cast<FreeSlot*>(curr)->next = central_lists[class_index];
        }

        curr = next;
    }

    central_lists[class_index] = reinterpret_cast<FreeSlot*>(start);
    return true;
}

// hands out up to max slots from central list.
FreeSlot* central_take(size_t class_index, size_t max, size_t& taken) {
    std::lock_guard<std::mutex> guard(class_locks[class_index]);
    taken = 0;

    if (central_lists[class_index] == nullptr && !populate_central_list(class_index)) {
        return nullptr;
    }

    FreeSlot* head = central_lists[class_index];
    FreeSlot* prev = nullptr;
    FreeSlot* current = head;

    while (current != nullptr && taken < max) {
        prev = current;
        current = current->next;
        ++taken;
    }

    prev->next = nullptr;  // cut the batch off from the rest of the list
    central_lists[class_index] = current;
    return head;
}

void central_return(size_t class_index, FreeSlot* head, FreeSlot* tail) {
    std::lock_guard<std::mutex> guard(class_locks[class_index]);
    tail->next = central_lists[class_index];
    central_lists[class_index] = head;
}

static void lock_all_classes() {
    for (std::mutex& lock : class_locks) {
        lock.lock();
    }
}

static void unlock_all_classes() {
    for (std::mutex& lock : class_locks) {
        lock.unlock();
    }
}

// Registers fork handlers
// lock all classes before forking and unlock after.
__attribute__((constructor)) static void register_fork_handlers() {
    pthread_atfork(lock_all_classes, unlock_all_classes, unlock_all_classes);
}

}  // namespace mtalloc