/* test.h: a minimal test runner, so the tests need no framework. */
#ifndef DK_TEST_H
#define DK_TEST_H

#include <stdio.h>
#include <string.h>

extern int test_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            test_failures++;                                                   \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do {                                                                       \
        const char *a_ = (a), *b_ = (b);                                       \
        if (!a_ || !b_ || strcmp(a_, b_) != 0) {                               \
            fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__,  \
                    __LINE__, b_ ? b_ : "(null)", a_ ? a_ : "(null)");          \
            test_failures++;                                                   \
        }                                                                      \
    } while (0)

/* Test helpers shared by the test files. */
char *test_tmpdir(void);
void test_write(const char *dir, const char *rel, const char *text);

void test_paths(void);
void test_repo(void);
void test_state(void);
void test_plan(void);

#endif
