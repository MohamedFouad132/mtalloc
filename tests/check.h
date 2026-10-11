// check.h: assertion and fingerprinting helpers shared by every test

#pragma once

#include <cstddef>      // size_t
#include <cstdint>      // uint64_t
#include <cstdio>       // fprintf
#include <cstdlib>      // abort


#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
            std::abort();                                                                  \
        }                                                                                  \
    } while (0)



inline unsigned char pattern_byte(uint64_t tag, size_t i) {
    return static_cast<unsigned char>((tag >> ((i % 8) * 8)) ^ i);
}

inline void fill(void* ptr, size_t size, uint64_t tag) {
    unsigned char* bytes = static_cast<unsigned char*>(ptr);
    for (size_t i = 0; i < size; i++) {
        bytes[i] = pattern_byte(tag, i);
    }
}

inline bool verify(const void* ptr, size_t size, uint64_t tag) {
    const unsigned char* bytes = static_cast<const unsigned char*>(ptr);
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != pattern_byte(tag, i)) {
            return false;
        }
    }
    return true;
}