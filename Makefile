CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -ftls-model=initial-exec

SRCS = $(wildcard src/*.cpp)

# Rule to build the allocator as a shared library
libmtalloc.so: $(SRCS) src/shared.h
	$(CXX) $(CXXFLAGS) -fPIC -shared -fvisibility=hidden -flto -o $@ $(SRCS)

// build without thread caches
libmtalloc_nocache.so: $(SRCS) src/shared.h
	$(CXX) $(CXXFLAGS) -DMTALLOC_NO_CACHE -fPIC -shared -fvisibility=hidden -flto -o $@ $(SRCS)

# Delete previous build
clean:
	rm -f libmtalloc.so libmtalloc_nocache.so