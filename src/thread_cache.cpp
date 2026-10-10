#include "shared.h"

#include <pthread.h>    // pthread_key_create, pthread_setspecific

namespace mtalloc {

struct ThreadCache {
    FreeSlot* heads[NUM_CLASSES];
    size_t counts[NUM_CLASSES];
};

static thread_local ThreadCache cache;
static thread_local bool cache_registered;
static pthread_key_t cache_key;

// Called by thread library whenever a registered thread exits.
static void flush_entire_cache(void*) {
    for (size_t class_index = 0; class_index < NUM_CLASSES; ++class_index) {
        FreeSlot* head = cache.heads[class_index];
        if (head == nullptr) {
            continue;
        }

        FreeSlot* tail = head;
        while (tail->next != nullptr) {
            tail = tail->next;
        }

        central_return(class_index, head, tail);
        cache.heads[class_index] = nullptr;
        cache.counts[class_index] = 0;
    }

    // Another library's exit cleanup may still free into this cache so let it re-register
    cache_registered = false;
}

__attribute__((constructor)) static void create_cache_key() {
    pthread_key_create(&cache_key, flush_entire_cache);
}

static void register_cache() {
    if (cache_registered) {
        return;
    }
    cache_registered = true;                  // set first since setspecific may call calloc
    pthread_setspecific(cache_key, &cache);   
}

static void* cache_pop(size_t class_index) {
    FreeSlot* slot = cache.heads[class_index];
    cache.heads[class_index] = slot->next;
    cache.counts[class_index]--;
    return slot;
}

static void cache_push(size_t class_index, FreeSlot* slot) {
    slot->next = cache.heads[class_index];
    cache.heads[class_index] = slot;
    cache.counts[class_index]++;
}

static void take_batch(size_t class_index) {
    size_t taken;
    cache.heads[class_index] = central_take(class_index, batch_size(class_index), taken);
    cache.counts[class_index] = taken;
}

static void return_batch(size_t class_index) {
    size_t n = batch_size(class_index);
    FreeSlot* head = cache.heads[class_index];
    FreeSlot* tail = head;
    for (size_t i = 1; i < n; ++i) {
        tail = tail->next;
    }

    cache.heads[class_index] = tail->next;
    cache.counts[class_index] -= n;
    central_return(class_index, head, tail);
}

void* cache_alloc(size_t class_index) {
    if (cache.heads[class_index] == nullptr) {
        register_cache();
        take_batch(class_index);
        if (cache.heads[class_index] == nullptr) {
            return nullptr;
        }
    }
    return cache_pop(class_index);
}

void cache_free(size_t class_index, void* ptr) {
    register_cache();
    cache_push(class_index, static_cast<FreeSlot*>(ptr));
    // Flush only past two batches so slots don't move back and forth between central list and cache.
    if (cache.counts[class_index] > 2 * batch_size(class_index)) {
        return_batch(class_index);
    }
}

}  // namespace mtalloc