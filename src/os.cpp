// os.cpp: all OS memory mappings and unmappings.

#include "shared.h"

#include <cerrno>       // errno, ENOMEM
#include <cstdint>      // uintptr_t, SIZE_MAX
#include <sys/mman.h>   // mmap, munmap, MAP_FAILED

namespace mtalloc {

void release_os_memory(void* ptr, size_t length) {
    munmap(ptr, length);
}

// Maps length bytes starting on a 64 KiB boundary.
// length must be a multiple of the page size.
void* get_os_memory(size_t length) {

    if (length > SIZE_MAX - CHUNK_SIZE){
        // Adding CHUNK_SIZE could overflow
        errno = ENOMEM;
        return nullptr;
    }

    // Map an extra CHUNK_SIZE so a 64 KiB boundary is guaranteed to exist inside
    size_t request = length + CHUNK_SIZE;
    void* mem = mmap(nullptr, request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        // Guarantee ENOMEM on allocation failure since mmap may set other errno values
        errno = ENOMEM; 
        return nullptr;
    }

    uintptr_t raw = reinterpret_cast<uintptr_t>(mem);
    uintptr_t aligned = (raw + CHUNK_MASK) & ~CHUNK_MASK;

    // Unmap the part before the boundary
    size_t unmap_front = static_cast<size_t>(aligned - raw);
    if (unmap_front > 0) {
        release_os_memory(mem, unmap_front);
    }

    // Unmap the part after the region
    uintptr_t request_end = raw + request;
    uintptr_t region_end = aligned + length;
    size_t unmap_end = static_cast<size_t>(request_end - region_end);
    if (unmap_end > 0) {
        release_os_memory(reinterpret_cast<void*>(region_end), unmap_end);
    }

    return reinterpret_cast<void*>(aligned);
}

} // namespace mtalloc