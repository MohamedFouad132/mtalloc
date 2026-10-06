#include <algorithm>    // std::max
#include <bit>          // std::has_single_bit, std::bit_ceil
#include <cerrno>       // errno, EINVAL, ENOMEM
#include <cstddef>      // size_t
#include <cstdint>      // uintptr_t
#include <cstdlib>      // official declarations to check our signatures
#include <cstring>      // memset, memcpy
#include <limits>       // std::numeric_limits

#include <malloc.h>     // malloc_usable_size, memalign, pvalloc
#include <sys/mman.h>   // mmap, munmap

constexpr size_t MIN_ALIGNMENT = 16;
constexpr size_t PAGE_SIZE = 4 * 1024;      // 4 KiB
constexpr size_t CHUNK_SIZE = 64 * 1024;    // 64 KiB
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

static size_t size_to_class(size_t size) {
    for (size_t class_index = 0; class_index < NUM_CLASSES; class_index++) {
        if (SIZE_CLASSES[class_index] >= size) {
            return class_index;
        }
    }
    return NUM_CLASSES;
}

static size_t round_up_to_page(size_t n) {
    return (n + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static bool multiply_overflows(size_t count, size_t size) {
    return size != 0 && count > std::numeric_limits<size_t>::max() / size;
}

static char* get_os_memory(size_t length) {
    size_t request = length + CHUNK_SIZE;
    void* mem = mmap(nullptr, request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        errno = ENOMEM;
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

    return reinterpret_cast<char*>(aligned);
}

static void write_label(char* region, size_t size_class, size_t length) {
    ChunkLabel* label = reinterpret_cast<ChunkLabel*>(region);
    label->size_class = size_class;
    label->length = length;
}

static ChunkLabel* label_of(void* ptr) {
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return reinterpret_cast<ChunkLabel*>(address & ~CHUNK_MASK);
}

static FreeSlot* pop_slot(size_t class_index) {
    FreeSlot* slot = free_lists[class_index];
    free_lists[class_index] = slot->next;
    return slot;
}

static void push_slot(size_t class_index, FreeSlot* slot) {
    slot->next = free_lists[class_index];
    free_lists[class_index] = slot;
}

static bool populate_free_list(size_t class_index) {
    char* chunk = get_os_memory(CHUNK_SIZE);
    if (chunk == nullptr) {
        return false;
    }

    write_label(chunk, class_index, CHUNK_SIZE);

    size_t slot_size = SIZE_CLASSES[class_index];
    size_t offset = std::has_single_bit(slot_size) ? slot_size : sizeof(ChunkLabel);
    char* start = chunk + offset;
    char* end = chunk + CHUNK_SIZE;
    size_t number_of_blocks = static_cast<size_t>(end - start) / slot_size;

    char* curr = start;
    for (size_t i = 0; i < number_of_blocks; i++) {
        char* next = curr + slot_size;
        if (i + 1 < number_of_blocks) {
            reinterpret_cast<FreeSlot*>(curr)->next = reinterpret_cast<FreeSlot*>(next);
        } else {
            reinterpret_cast<FreeSlot*>(curr)->next = free_lists[class_index];
        }

        curr = next;
    }

    free_lists[class_index] = reinterpret_cast<FreeSlot*>(start);
    return true;
}

static void* malloc_large(size_t size, size_t offset) {
    if (size > std::numeric_limits<size_t>::max() - (offset + PAGE_SIZE + CHUNK_SIZE)) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t length = round_up_to_page(offset + size);
    char* region = get_os_memory(length);
    if (region == nullptr) {
        return nullptr;
    }

    write_label(region, NUM_CLASSES, length);

    return region + offset;
}

static void* aligned_malloc(size_t alignment, size_t size) {
    if (alignment <= MIN_ALIGNMENT) {
        return malloc(size);
    }

    if (alignment >= CHUNK_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t needed = std::max(size, alignment);
    if (needed <= SIZE_CLASSES[NUM_CLASSES - 1]) {
        return malloc(std::bit_ceil(needed));
    }

    return malloc_large(size, alignment);
}

extern "C" {

void* malloc(size_t size) noexcept {
    size_t class_index = size_to_class(size);

    if (class_index == NUM_CLASSES) {
        return malloc_large(size, sizeof(ChunkLabel));
    }

    if (free_lists[class_index] == nullptr) {
        if (!populate_free_list(class_index)) {
            return nullptr;
        }
    }

    return pop_slot(class_index);
}

void free(void* ptr) noexcept {
    if (ptr == nullptr) {
        return;
    }

    ChunkLabel* label = label_of(ptr);
    size_t class_index = label->size_class;

    if (class_index == NUM_CLASSES) {
        munmap(label, label->length);
        return;
    }

    push_slot(class_index, static_cast<FreeSlot*>(ptr));
}

void* calloc(size_t count, size_t size) noexcept {
    if (multiply_overflows(count, size)) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t total = count * size;
    void* mem_start = malloc(total);
    if (mem_start != nullptr) {
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

    size_t old_size = malloc_usable_size(ptr);
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

void* reallocarray(void* ptr, size_t count, size_t size) noexcept {
    if (multiply_overflows(count, size)) {
        errno = ENOMEM;
        return nullptr;
    }

    return realloc(ptr, count * size);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) noexcept {
    if (!std::has_single_bit(alignment) || alignment % sizeof(void*) != 0) {
        return EINVAL;
    }

    void* mem = aligned_malloc(alignment, size);
    if (mem == nullptr) {
        return ENOMEM;
    }

    *memptr = mem;
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) noexcept {
    if (!std::has_single_bit(alignment)) {
        errno = EINVAL;
        return nullptr;
    }

    return aligned_malloc(alignment, size);
}

void* memalign(size_t alignment, size_t size) noexcept {
    if (!std::has_single_bit(alignment)) {
        if (alignment > CHUNK_SIZE) {
            errno = EINVAL;
            return nullptr;
        }

        alignment = std::bit_ceil(alignment);
    }

    return aligned_malloc(alignment, size);
}

void* valloc(size_t size) noexcept {
    return aligned_malloc(PAGE_SIZE, size);
}

void* pvalloc(size_t size) noexcept {
    if (size > std::numeric_limits<size_t>::max() - PAGE_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t rounded = round_up_to_page(size);
    if (rounded == 0) {
        rounded = PAGE_SIZE;
    }

    return aligned_malloc(PAGE_SIZE, rounded);
}

size_t malloc_usable_size(void* ptr) noexcept {
    if (ptr == nullptr) {
        return 0;
    }

    ChunkLabel* label = label_of(ptr);

    if (label->size_class == NUM_CLASSES) {
        char* region_end = reinterpret_cast<char*>(label) + label->length;
        return static_cast<size_t>(region_end - static_cast<char*>(ptr));
    }

    return SIZE_CLASSES[label->size_class];
}

}
