#pragma once

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <cstdint>

inline uint64_t cg_parse_u64(const char* value, const char* name = "integer", int base = 10) {
    if (!value || !*value || value[0] == '-' || value[0] == '+' ||
        value[0] < '0' || value[0] > '9')
        throw std::runtime_error(std::string("invalid unsigned integer: ") + name);
    errno = 0;
    char* end = nullptr;
    auto result = std::strtoull(value, &end, base);
    if (errno == ERANGE || end == value || *end)
        throw std::runtime_error(std::string("invalid or overflowing unsigned integer: ") + name);
    return result;
}

inline size_t cg_max_items() {
    const char* value = std::getenv("CG_MAX_ITEMS");
    uint64_t limit = value && *value ? cg_parse_u64(value, "CG_MAX_ITEMS") : (1ULL << 20);
    if (!limit || limit > (1ULL << 24))
        throw std::runtime_error("CG_MAX_ITEMS must be in [1,16777216]");
    return static_cast<size_t>(limit);
}

inline size_t cg_parse_size(const char* value, const char* name = "count", bool allow_zero = false,
                            size_t maximum = 0) {
    auto n = cg_parse_u64(value, name);
    if ((!allow_zero && n == 0) || n > (maximum ? maximum : cg_max_items()) ||
        n > std::numeric_limits<size_t>::max())
        throw std::runtime_error(std::string("out-of-range count: ") + name);
    return static_cast<size_t>(n);
}

inline size_t cg_config_size(const char* name, size_t fallback, size_t maximum = 0,
                             bool allow_zero = false) {
    const char* value = std::getenv(name);
    return value && *value ? cg_parse_size(value, name, allow_zero, maximum) : fallback;
}

inline size_t cg_checked_add(size_t a, size_t b) {
    if (a > cg_max_items() || b > cg_max_items() - a)
        throw std::runtime_error("combined item count exceeds CG_MAX_ITEMS");
    return a + b;
}

inline void cg_validate_count(size_t n, bool allow_zero = false) {
    if ((!allow_zero && !n) || n > cg_max_items())
        throw std::runtime_error("item count outside CG_MAX_ITEMS");
}
