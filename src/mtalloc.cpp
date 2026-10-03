#include <limits>
#include <sys/mman.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>


constexpr size_t CHUNK_SIZE = 1024 * 1024; // ask the OS for 1 MiB at a time
constexpr size_t HEADER_SIZE = 16; // space before a block to store its size


static char* next_free = nullptr;
static size_t capacity_left = 0; 

static bool get_chunk(size_t minimum_size){

    size_t size;
    if (minimum_size > CHUNK_SIZE){
        size = minimum_size;
    } else {
        size = CHUNK_SIZE;
    }

    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED){
        return false;
    }
    next_free = static_cast<char*>(mem);
    capacity_left = size;
    return true;



} 

extern "C" {

void* malloc(size_t size){
    if (size > std::numeric_limits<size_t>::max() - 64){
        return nullptr;
    }

    size_t rounded = (size + 15) & ~static_cast<size_t>(15);
    size_t needed = rounded + HEADER_SIZE;

    if (needed > capacity_left){
        if(!get_chunk(needed)){
            return nullptr;
        }
    }

    char* block = next_free;
    next_free += needed;
    capacity_left -= needed;

    *(reinterpret_cast<size_t*>(block)) = rounded;

    return block + HEADER_SIZE;


}

void free([[maybe_unused]] void* ptr) {}


void* calloc(size_t count, size_t size){
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

void* realloc(void* ptr, size_t size){

    if (ptr == nullptr){
        return malloc(size);
    }

    if (size == 0){
        free(ptr);
        return nullptr;
    }

    char* block = static_cast<char*>(ptr) -  HEADER_SIZE;
    size_t old_size = *(reinterpret_cast<size_t*>(block));

    if (size <= old_size){
        return ptr;
    }

    void* new_mem = malloc(size);
    if (new_mem == nullptr){
        return nullptr;
    }

    memcpy(new_mem, ptr, old_size);
    free(ptr);
    return new_mem;

}

}