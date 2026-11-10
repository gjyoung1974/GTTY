#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)
#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    auto _a = (a); auto _b = (b);                                                          \
    if (!(_a == _b)) {                                                                     \
      std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)
#define TEST_MAIN_END() \
  do { if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; } \
       std::printf("OK\n"); return 0; } while (0)
