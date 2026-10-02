#include "test.h"

#include <stdlib.h>

#include "../../src/platform/paths.h"
#include "../../src/core/repo.h"

static void check_join(const char *a, const char *b, const char *want)
{
    char *got = dk_path_join(a, b);
    CHECK_STR(got, want);
    free(got);
}

static void check_abs(const char *in, const char *want)
{
    dk_err err;
    char *got = dk_abspath(in, &err);
    CHECK_STR(got, want);
    free(got);
}

void test_paths(void)
{
    check_join("/a", "b", "/a/b");
    check_join("/a/", "/b", "/a/b");
    check_join("/", "b", "/b");
    check_join("/a", "", "/a");

    check_abs("/a//b/./c/", "/a/b/c");
    check_abs("/", "/");
    char *want = dk_path_join(dk_home(), ".dotfiles");
    check_abs("~/.dotfiles", want);
    free(want);

    char *t = dk_tildify(dk_home());
    CHECK_STR(t, "~");
    free(t);
    t = dk_tildify("/elsewhere");
    CHECK_STR(t, "/elsewhere");
    free(t);

    char *d = dk_dirname("/a/b");
    CHECK_STR(d, "/a");
    free(d);
    d = dk_dirname("/a");
    CHECK_STR(d, "/");
    free(d);

    CHECK(dk_name_valid("nvim"));
    CHECK(dk_name_valid("work-laptop_2.0"));
    CHECK(!dk_name_valid(""));
    CHECK(!dk_name_valid(".hidden"));
    CHECK(!dk_name_valid("a/b"));
    CHECK(!dk_name_valid("with space"));
}
