#pragma once

// Marks a public function as exported (everything else is hidden by -fvisibility=hidden)
#define MTALLOC_API __attribute__((visibility("default")))

   #include <algorithm>    // std::clamp
   #include <bit>          // std::has_single_bit
   #include <cstddef>      // size_t
   #include <cstdint>      // uintptr_t, SIZE_MAX

namespace mtalloc {

inline constexpr size_t MIN_ALIGNMENT = 16;
inline constexpr size_t PAGE_SIZE     = 4 * 1024;     // 4 KiB
inline constexpr size_t CHUNK_SIZE    = 64 * 1024;    // 64 KiB
inline constexpr size_t BATCH_SIZE    = 16 * 1024;       // 16 KiB
inline constexpr uintptr_t CHUNK_MASK = static_cast<uintptr_t>(CHUNK_SIZE) - 1;

inline constexpr size_t NUM_CLASSES = 32;
inline constexpr size_t SIZE_CLASSES[NUM_CLASSES] = {
    16,   32,   48,   64,   80,   96,   112,  128,
    160,  192,  224,  256,
    320,  384,  448,  512,
    640,  768,  896,  1024,
    1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096,
    5120, 6144, 7168, 8192,
};

// Stored at the start of every chunk and large mapping
// A pointer's header is found by rounding down to the nearest 64 KiB
struct ChunkHeader {
    size_t class_index;  // classes 0-31 or NUM_CLASSES for large mappings
    size_t length;       // bytes in this mapping
};

static_assert(sizeof(ChunkHeader) == MIN_ALIGNMENT);

struct FreeSlot {
    FreeSlot* next;
};


constexpr size_t class_alignment(size_t class_index) {
    size_t slot_size = SIZE_CLASSES[class_index];
    // power of 2 classes are aligned to their size.
    // the rest start after the header so are aligned to MIN_ALIGNMENT 
    return std::has_single_bit(slot_size) ? slot_size : MIN_ALIGNMENT;
}

// A class must be big enough and have an alignment that is a multiple of the requested alignment.
inline size_t class_for(size_t size, size_t alignment) {
    for (size_t class_index = 0; class_index < NUM_CLASSES; class_index++) {
        if (SIZE_CLASSES[class_index] >= size && class_alignment(class_index) >= alignment) {
            return class_index;
        }
    }
    return NUM_CLASSES;
}

// Target 16 KiB per batch but bounded between 2 and 32 slots
constexpr size_t batch_size(size_t class_index) {
    size_t slots = BATCH_SIZE / SIZE_CLASSES[class_index];
    return std::clamp(slots, size_t{2}, size_t{32});
}

inline size_t round_up_to_page(size_t n) {
    return (n + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

inline bool multiply_overflows(size_t count, size_t size) {
    return size != 0 && count > SIZE_MAX / size;
}

inline void write_header(char* region, size_t class_index, size_t length) {
    ChunkHeader* header = reinterpret_cast<ChunkHeader*>(region);
    header->class_index = class_index;
    header->length = length;
}

inline ChunkHeader* header_of(void* ptr) {
    uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return reinterpret_cast<ChunkHeader*>(address & ~CHUNK_MASK);
}

// os.cpp declarations
void release_os_memory(void* ptr, size_t length);
void* get_os_memory(size_t length);

// central.cpp declarations
FreeSlot* central_take(size_t class_index, size_t max, size_t& taken);
void central_return(size_t class_index, FreeSlot* head, FreeSlot* tail);

// thread_cache.cpp declarations
void* cache_alloc(size_t class_index);
void cache_free(size_t class_index, void* ptr);

// large.cpp declarations
void* large_alloc(size_t size, size_t alignment);
void large_free(ChunkHeader* header);
size_t large_usable_size(ChunkHeader* header, void* ptr);

} // namespace mtalloc