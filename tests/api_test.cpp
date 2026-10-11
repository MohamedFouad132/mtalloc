// api_test.cpp: checks the behavior of every public allocation function

#include "check.h"

#include <cerrno>       // errno, ENOMEM, EINVAL
#include <cstdint>      // uintptr_t, SIZE_MAX
#include <cstdlib>      // malloc, free, calloc, realloc, aligned_alloc
#include <cstring>      // memset
#include <malloc.h>     // malloc_usable_size, memalign, valloc, pvalloc

// Sizes around the edges where off-by-one errors are most likely to happen
static constexpr size_t SIZES[] = {
    0, 1, 15, 16, 17, 127, 128, 129, 4095, 4096, 4097,
    8191, 8192, 8193, 65536, size_t{1} << 20,
};

static uint64_t next_tag = 1;

static bool is_aligned(const void* ptr, size_t alignment) {
    return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
}

static bool all_zero(const void* ptr, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(ptr);
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

// Checks a slot is non-null, aligned, big enough and fully writable
static void check_usable(void* ptr, size_t size, size_t alignment) {
    CHECK(ptr != nullptr);
    CHECK(is_aligned(ptr, alignment));
    CHECK(malloc_usable_size(ptr) >= size);
    memset(ptr, 0xCD, size);
}

static void test_malloc_free() {
    free(nullptr);
    CHECK(malloc_usable_size(nullptr) == 0);

    for (size_t size : SIZES) {
        void* ptr = malloc(size);
        check_usable(ptr, size, 16);
        free(ptr);
    }
}

static void test_many_live_slots() {
    constexpr size_t COUNT = 1000;
    constexpr size_t NUM_SIZES = sizeof(SIZES) / sizeof(SIZES[0]);
    void* ptrs[COUNT];
    uint64_t tags[COUNT];

    for (size_t i = 0; i < COUNT; i++) {
        size_t size = SIZES[i % NUM_SIZES];
        ptrs[i] = malloc(size);
        CHECK(ptrs[i] != nullptr);
        tags[i] = next_tag++;
        fill(ptrs[i], size, tags[i]);
    }

    for (size_t i = 0; i < COUNT; i++) {
        CHECK(verify(ptrs[i], SIZES[i % NUM_SIZES], tags[i]));
        free(ptrs[i]);
    }
}

static void test_calloc() {
    // Dirty a slot then free it.
    // Since the cache is LIFO calloc should get the same slot
    void* dirty = malloc(64);
    CHECK(dirty != nullptr);
    memset(dirty, 0xAB, 64);
    free(dirty);

    void* ptr = calloc(1, 64);
    CHECK(ptr == dirty);
    CHECK(all_zero(ptr, 64));
    free(ptr);

    ptr = calloc(1000, 1000);
    CHECK(ptr != nullptr);
    CHECK(all_zero(ptr, 1000 * 1000));
    free(ptr);

    ptr = calloc(0, 16);
    CHECK(ptr != nullptr);
    free(ptr);

    ptr = calloc(16, 0);
    CHECK(ptr != nullptr);
    free(ptr);
}

static void test_overflow() {
    // volatile stops the compiler from warning about or skipping these calls
    volatile size_t huge = SIZE_MAX; // should be caught by the overflow check
    volatile size_t too_big = size_t{1} << 50; // should be rejected by mmap

    errno = 0;
    CHECK(malloc(huge) == nullptr);
    CHECK(errno == ENOMEM);

    errno = 0;
    CHECK(malloc(too_big) == nullptr);
    CHECK(errno == ENOMEM);

    errno = 0;
    CHECK(calloc(huge, 2) == nullptr);
    CHECK(errno == ENOMEM);

    errno = 0;
    CHECK(reallocarray(nullptr, huge, 2) == nullptr);
    CHECK(errno == ENOMEM);
}

static void test_realloc() {
    void* ptr = realloc(nullptr, 24);
    CHECK(ptr != nullptr);
    uint64_t tag = next_tag++;
    fill(ptr, 24, tag);
    size_t old_size = 24;

    const size_t GROW_SIZES[] = {100, 5000, 20000, size_t{1} << 20};
    for (size_t size : GROW_SIZES) {
        ptr = realloc(ptr, size);
        CHECK(ptr != nullptr);
        CHECK(is_aligned(ptr, 16));
        CHECK(verify(ptr, old_size, tag));
        fill(ptr, size, tag);
        old_size = size;
    }

    ptr = realloc(ptr, 10);
    CHECK(ptr != nullptr);
    CHECK(verify(ptr, 10, tag));

    CHECK(realloc(ptr, 0) == nullptr);  // frees ptr

    ptr = reallocarray(nullptr, 10, 8);
    check_usable(ptr, 80, 16);
    free(ptr);
}

static void test_aligned() {
    const size_t ALIGNED_SIZES[] = {1, 100, 10000};
    for (size_t alignment = 16; alignment <= 32768; alignment *= 2) {
        for (size_t size : ALIGNED_SIZES) {
            void* ptr = aligned_alloc(alignment, size);
            check_usable(ptr, size, alignment);
            free(ptr);

            ptr = memalign(alignment, size);
            check_usable(ptr, size, alignment);
            free(ptr);

            void* out = nullptr;
            CHECK(posix_memalign(&out, alignment, size) == 0);
            check_usable(out, size, alignment);
            free(out);
        }
    }

    // check that posix is not writing to out for rejected alignments
    void* untouched = &next_tag;
    void* out = untouched;
    CHECK(posix_memalign(&out, 4, 100) == EINVAL);   // power of two but smaller than a pointer
    CHECK(posix_memalign(&out, 24, 100) == EINVAL);  // not a power of two
    CHECK(out == untouched);

    void* ptr = memalign(24, 100);
    check_usable(ptr, 100, 32);
    free(ptr);

    errno = 0;
    CHECK(aligned_alloc(65536, 100) == nullptr);  // alignments of 64 KiB and up aren't supported
    CHECK(errno == ENOMEM);

    errno = 0;
    CHECK(aligned_alloc(24, 100) == nullptr);     // not a power of two
    CHECK(errno == EINVAL);
}

static void test_page_aligned() {
    void* ptr = valloc(100);
    check_usable(ptr, 100, 4096);
    free(ptr);

    ptr = pvalloc(100);
    check_usable(ptr, 4096, 4096);
    free(ptr);
}

int main() {
    test_malloc_free();
    test_many_live_slots();
    test_calloc();
    test_overflow();
    test_realloc();
    test_aligned();
    test_page_aligned();

    std::printf("api_test: all checks passed\n");
    return 0;
}