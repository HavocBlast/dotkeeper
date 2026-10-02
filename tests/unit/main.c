#include "test.h"

#include <stdlib.h>
#include <unistd.h>

#include "../../src/platform/fs.h"
#include "../../src/platform/paths.h"

int test_failures;

char *test_tmpdir(void)
{
    char tmpl[] = "/tmp/dotkeeper-test-XXXXXX";
    if (!mkdtemp(tmpl)) {
        perror("mkdtemp");
        exit(1);
    }
    /* Resolve /tmp -> /private/tmp on macOS so paths compare equal. */
    char buf[4096];
    return xstrdup(realpath(tmpl, buf) ? buf : tmpl);
}

void test_write(const char *dir, const char *rel, const char *text)
{
    dk_err err;
    char *path = dk_path_join(dir, rel);
    if (dk_write_file(path, text, strlen(text), &err) != 0) {
        fprintf(stderr, "test_write: %s\n", err.msg);
        exit(1);
    }
    free(path);
}

int main(void)
{
    /* Keep tests away from the real home directory. */
    char *home = test_tmpdir();
    setenv("HOME", home, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_STATE_HOME");

    test_paths();
    test_repo();
    test_state();
    test_plan();

    free(home);
    if (test_failures) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return 1;
    }
    printf("unit tests passed\n");
    return 0;
}
