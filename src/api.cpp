// api.cpp: the public malloc API

#include "shared.h"

#include <bit>          // std::has_single_bit, std::bit_ceil
#include <cerrno>       // errno, ENOMEM, EINVAL
#include <cstring>      // memset, memcpy

namespace mtalloc {

// Common path for every allocating call
static void* allocate(size_t size, size_t alignment) {
    size_t class_index = class_for(size, alignment);
    if (class_index == NUM_CLASSES) {
        return large_alloc(size, alignment);
    }
    return cache_alloc(class_index);
}

// Common path for every freeing call
static void deallocate(void* ptr) {
    if (ptr == nullptr) {
        return;
    }

    ChunkHeader* header = header_of(ptr);
    if (header->class_index == NUM_CLASSES) {
        large_free(header);
        return;
    }
    cache_free(header->class_index, ptr);
}

static size_t usable_size(void* ptr) {
    ChunkHeader* header = header_of(ptr);
    if (header->class_index == NUM_CLASSES) {
        return large_usable_size(header, ptr);
    }
    return SIZE_CLASSES[header->class_index];
}

}  // namespace mtalloc

extern "C" {

MTALLOC_API void* malloc(size_t size) noexcept {
    return mtalloc::allocate(size, mtalloc::MIN_ALIGNMENT);
}

MTALLOC_API void free(void* ptr) noexcept {
    mtalloc::deallocate(ptr);
}

MTALLOC_API void* calloc(size_t count, size_t size) noexcept {
    if (mtalloc::multiply_overflows(count, size)) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t total = count * size;
    void* ptr = mtalloc::allocate(total, mtalloc::MIN_ALIGNMENT);
    if (ptr != nullptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

MTALLOC_API void* realloc(void* ptr, size_t size) noexcept {
    if (ptr == nullptr) {
        return mtalloc::allocate(size, mtalloc::MIN_ALIGNMENT);
    }
    if (size == 0) {
        mtalloc::deallocate(ptr);
        return nullptr;
    }

    // Already big enough
    size_t old_size = mtalloc::usable_size(ptr);
    if (size <= old_size) {
        return ptr;
    }

    void* new_ptr = mtalloc::allocate(size, mtalloc::MIN_ALIGNMENT);
    if (new_ptr == nullptr) {
        return nullptr;  // old block stays valid
    }
    memcpy(new_ptr, ptr, old_size);
    mtalloc::deallocate(ptr);
    return new_ptr;
}

MTALLOC_API void* reallocarray(void* ptr, size_t count, size_t size) noexcept {
    if (mtalloc::multiply_overflows(count, size)) {
        errno = ENOMEM;
        return nullptr;
    }
    return realloc(ptr, count * size);
}

MTALLOC_API size_t malloc_usable_size(void* ptr) noexcept {
    if (ptr == nullptr) {
        return 0;
    }
    return mtalloc::usable_size(ptr);
}

MTALLOC_API int posix_memalign(void** out, size_t alignment, size_t size) noexcept {
    if (!std::has_single_bit(alignment) || alignment % sizeof(void*) != 0) {
        return EINVAL;
    }

    void* ptr = mtalloc::allocate(size, alignment);
    if (ptr == nullptr) {
        return ENOMEM;
    }
    *out = ptr;
    return 0;
}

MTALLOC_API void* aligned_alloc(size_t alignment, size_t size) noexcept {
    if (!std::has_single_bit(alignment)) {
        errno = EINVAL;
        return nullptr;
    }
    return mtalloc::allocate(size, alignment);
}

// Rounds to a power of 2 instead of rejecting non power of 2 alignments
MTALLOC_API void* memalign(size_t alignment, size_t size) noexcept {
    if (alignment >= mtalloc::CHUNK_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }
    return mtalloc::allocate(size, std::bit_ceil(alignment));
}

MTALLOC_API void* valloc(size_t size) noexcept {
    return mtalloc::allocate(size, mtalloc::PAGE_SIZE);
}

// valloc + size is rounded up to a multiple of PAGE_SIZE.
MTALLOC_API void* pvalloc(size_t size) noexcept {
    if (size > SIZE_MAX - mtalloc::PAGE_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }
    return mtalloc::allocate(mtalloc::round_up_to_page(size), mtalloc::PAGE_SIZE);
}

}  // extern "C"