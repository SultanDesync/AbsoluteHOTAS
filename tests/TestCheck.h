#pragma once

#include <cstdio>
#include <cstdlib>

// Evaluate once in every build mode, including when NDEBUG is defined. Failure
// stops immediately so later checks cannot dereference uninitialized test data.
#define REQUIRE(...) do { \
    if (!(__VA_ARGS__)) { \
        std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)
