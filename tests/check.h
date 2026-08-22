#pragma once
#include <cstdio>
static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; if (!((a) == (b))) { ++g_fails; std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)
#define CHECK_STREQ(a, b) do { ++g_checks; if (std::strcmp((a), (b)) != 0) { ++g_fails; std::fprintf(stderr, "%s:%d: CHECK_STREQ failed: '%s' vs '%s'\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define TEST_MAIN() int main() { run(); std::printf("%d checks, %d failed\n", g_checks, g_fails); return g_fails ? 1 : 0; }
