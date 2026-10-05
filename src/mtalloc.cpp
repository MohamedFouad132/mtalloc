#include <sys/mman.h>   // mmap, munmap


#include <cstddef>      // size_t
#include <cstdint>      // uintptr_t
#include <cstdlib>      // official declarations to check our signatures
#include <cstring>      // memset, memcpy
#include <limits>       // std::numeric_limits
#include <bit>          // std::has_single_bit

constexpr size_t PAGE_SIZE = 4 * 1024; // 4 KiB
constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KiB
constexpr uintptr_t CHUNK_MASK = static_cast<uintptr_t>(CHUNK_SIZE) - 1;
constexpr size_t NUM_CLASSES = 32;
constexpr size_t SIZE_CLASSES[NUM_CLASSES] = {
    16,   32,   48,   64,   80,   96,   112,  128,   
    160,  192,  224,  256,                           
    320,  384,  448,  512,
    640,  768,  896,  1024,
    1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096,
    5120, 6144, 7168, 8192,
};

struct ChunkLabel {
    size_t size_class;
    size_t length;
};

struct FreeSlot {
    FreeSlot* next;
};

static FreeSlot* free_lists[NUM_CLASSES]; 


static size_t size_to_class(size_t size){
    for (size_t class_index = 0; class_index < NUM_CLASSES; class_index++){
        if (SIZE_CLASSES[class_index] >= size){
            return class_index;
        }
    }
    return NUM_CLASSES;
}

static char* get_aligned_chunk(){
    size_t request = CHUNK_SIZE * 2;
    void* mem = mmap(nullptr, request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED){
        return nullptr;
    }
    uintptr_t raw = reinterpret_cast<uintptr_t>(mem);
    uintptr_t aligned = (raw + CHUNK_MASK) & ~CHUNK_MASK;

    size_t unmap_front = static_cast<size_t>(aligned - raw);

    if (unmap_front > 0){
        munmap(mem, unmap_front);
    }

    uintptr_t request_end = raw + request;
    uintptr_t chunk_end = aligned + CHUNK_SIZE;

    size_t unmap_end = static_cast<size_t>(request_end - chunk_end);


    if (unmap_end > 0){
        munmap(reinterpret_cast<void*>(chunk_end), unmap_end);
    }

    return reinterpret_cast<char*>(aligned);
} 

static bool populate_free_list(size_t class_index){
    char* chunk = get_aligned_chunk();
    if (chunk == nullptr){
        return false;
    }

    ChunkLabel* label = reinterpret_cast<ChunkLabel*>(chunk);
    label->size_class = class_index;
    label->length = CHUNK_SIZE;
    
    size_t slot_size = SIZE_CLASSES[class_index];
    size_t offset = std::has_single_bit(slot_size) ? slot_size : sizeof(ChunkLabel);
    char* start = chunk + offset;
    char* end = chunk + CHUNK_SIZE;
    size_t number_of_blocks = static_cast<size_t>(end - start) / slot_size;

    char* curr = start;
    for (size_t i = 0; i < number_of_blocks; i++){
        char* next = curr + slot_size;
        if (i + 1 < number_of_blocks){
            (reinterpret_cast<FreeSlot*>(curr))->next = reinterpret_cast<FreeSlot*>(next);
        } else {
            (reinterpret_cast<FreeSlot*>(curr))->next = free_lists[class_index];
        }

        curr = next;
    }

    free_lists[class_index] = reinterpret_cast<FreeSlot*>(start);
    return true;
}


static void* malloc_large(size_t size) {
    if (size > std::numeric_limits<size_t>::max() - (sizeof(ChunkLabel) + PAGE_SIZE + CHUNK_SIZE)){
        return nullptr;
    }

    size_t length = (sizeof(ChunkLabel) + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t request = length + CHUNK_SIZE;
    void* mem = mmap(nullptr, request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        return nullptr;
    }



    uintptr_t raw = reinterpret_cast<uintptr_t>(mem);
    uintptr_t aligned = (raw + CHUNK_MASK) & ~CHUNK_MASK;

    size_t unmap_front = static_cast<size_t>(aligned - raw);
    if (unmap_front > 0) {
        munmap(mem, unmap_front);
    }

    uintptr_t request_end = raw + request;
    uintptr_t region_end = aligned + length;
    size_t unmap_end = static_cast<size_t>(request_end - region_end);
    if (unmap_end > 0) {
        munmap(reinterpret_cast<void*>(region_end), unmap_end);
    }

    ChunkLabel* label = reinterpret_cast<ChunkLabel*>(aligned);
    label->size_class = NUM_CLASSES;
    label->length = length;

    return reinterpret_cast<char*>(aligned) + sizeof(ChunkLabel);
}

extern "C" {

void* malloc(size_t size) noexcept {
    size_t class_index = size_to_class(size);

    if (class_index == NUM_CLASSES){
        return malloc_large(size);
    }

    if (free_lists[class_index] == nullptr){
        if(!populate_free_list(class_index)){
            return nullptr;
        }
    }
        
    FreeSlot* slot = free_lists[class_index];
    free_lists[class_index] = slot->next;
    return slot;

}

void free(void* ptr) noexcept {
    if (ptr == nullptr){
        return;
    }

    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t chunk_start = address & ~CHUNK_MASK;
    ChunkLabel* label = reinterpret_cast<ChunkLabel*>(chunk_start);
    size_t class_index = label->size_class;

    if (class_index == NUM_CLASSES){
        munmap(label, label->length);
        return;
    }
    FreeSlot* new_head = static_cast<FreeSlot*>(ptr);
    new_head->next = free_lists[class_index];
    free_lists[class_index] = new_head;
}


void* calloc(size_t count, size_t size) noexcept {
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size){
        return nullptr;
    }

    size_t total = count * size;
    void* mem_start = malloc(total);
    if (mem_start != nullptr){
        memset(mem_start, 0, total);
    }

    return mem_start;
}

void* realloc(void* ptr, size_t size) noexcept {
    if (ptr == nullptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return nullptr;
    }

    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t chunk_start = address & ~CHUNK_MASK;
    ChunkLabel* label = reinterpret_cast<ChunkLabel*>(chunk_start);

    size_t old_size;
    if (label->size_class == NUM_CLASSES) {
        old_size = label->length - sizeof(ChunkLabel);
    } else {
        old_size = SIZE_CLASSES[label->size_class];
    }

    if (size <= old_size) {
        return ptr;
    }

    void* new_mem = malloc(size);
    if (new_mem == nullptr) {
        return nullptr;
    }
    memcpy(new_mem, ptr, old_size);
    free(ptr);
    return new_mem;
}

}