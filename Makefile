CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -ftls-model=initial-exec

# Rule to build the allocator as a shared library
libmtalloc.so: src/mtalloc.cpp
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $<


# Delete previous build
clean:
	rm -f libmtalloc.so