#pragma once
#include <cstdlib>
#include <string>

inline std::string env(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return v ? v : def;
}
