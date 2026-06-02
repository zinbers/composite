#pragma once
#include <cstdio>

// Simple stderr logger. Printf-style format strings.
#define LOG_INFO(fmt, ...)  do { std::fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { std::fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...) do { std::fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)
