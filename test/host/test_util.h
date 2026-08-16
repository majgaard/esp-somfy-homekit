#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_failures;
extern int g_checks;

#define RUN(fn)                                                                \
    do {                                                                       \
        int before = g_failures;                                               \
        fn();                                                                  \
        printf("  %-44s %s\n", #fn,                                            \
               (g_failures == before) ? "ok" : "FAIL");                        \
    } while (0)

#define FAILF(...)                                                             \
    do {                                                                       \
        g_failures++;                                                          \
        printf("    %s:%d: ", __FILE__, __LINE__);                             \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            FAILF("expected true: %s", #cond);                                 \
        }                                                                      \
    } while (0)

#define ASSERT_EQ(want, got)                                                   \
    do {                                                                       \
        g_checks++;                                                            \
        long long w_ = (long long)(want), g_ = (long long)(got);               \
        if (w_ != g_) {                                                        \
            FAILF("%s: want %lld, got %lld", #got, w_, g_);                    \
        }                                                                      \
    } while (0)

#define ASSERT_NEAR(want, got, tol)                                            \
    do {                                                                       \
        g_checks++;                                                            \
        long long w_ = (long long)(want), g_ = (long long)(got);               \
        long long d_ = w_ > g_ ? w_ - g_ : g_ - w_;                            \
        if (d_ > (long long)(tol)) {                                           \
            FAILF("%s: want %lld +/- %lld, got %lld", #got, w_,                \
                  (long long)(tol), g_);                                       \
        }                                                                      \
    } while (0)

#define ASSERT_MEM_EQ(want, got, len)                                          \
    do {                                                                       \
        g_checks++;                                                            \
        if (memcmp((want), (got), (len)) != 0) {                               \
            g_failures++;                                                      \
            printf("    %s:%d: %s mismatch\n      want:", __FILE__, __LINE__,  \
                   #got);                                                      \
            for (size_t i_ = 0; i_ < (size_t)(len); i_++)                      \
                printf(" %02X", ((const unsigned char *)(want))[i_]);          \
            printf("\n      got: ");                                           \
            for (size_t i_ = 0; i_ < (size_t)(len); i_++)                      \
                printf(" %02X", ((const unsigned char *)(got))[i_]);           \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#endif /* TEST_UTIL_H */
