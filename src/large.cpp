#include "shared.h"

#include <cerrno>       // errno, ENOMEM

namespace mtalloc {

void* large_alloc(size_t size, size_t alignment) {

    if (alignment >= CHUNK_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t offset = std::max(alignment, sizeof(ChunkHeader));

    if (size > SIZE_MAX - offset - PAGE_SIZE) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t length = round_up_to_page(offset + size);
    char* region = static_cast<char*>(get_os_memory(length));
    if (region == nullptr) {
        return nullptr;
    }

    write_header(region, NUM_CLASSES, length);
    return region + offset;
}

void large_free(ChunkHeader* header) {
    release_os_memory(header, header->length);
}

size_t large_usable_size(ChunkHeader* header, void* ptr) {
    size_t offset = static_cast<size_t>(static_cast<char*>(ptr) - reinterpret_cast<char*>(header));
    return header->length - offset;
}

}  // namespace mtalloc