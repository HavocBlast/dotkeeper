#include "test.h"

#include <stdlib.h>

#include "../../src/core/state.h"
#include "../../src/platform/paths.h"

void test_state(void)
{
    dk_err err;
    char *dir = test_tmpdir();
    char *path = dk_path_join(dir, "state.ini");

    dk_state s = {0};
    CHECK(dk_state_load(path, &s, &err) == 0); /* missing file = empty */
    CHECK(s.nlinks == 0 && s.sel.preset == NULL);

    dk_selection_set_preset(&s.sel, "work");
    dk_selection_set_override(&s.sel, "nvim", "minimal");
    dk_state_add_link(&s, "/h/.zshrc", "/r/apps/zsh/work/.zshrc");
    dk_state_add_link(&s, "/h/odd; path = x", "/r/a: b");
    CHECK(dk_state_save(path, &s, &err) == 0);
    dk_state_free(&s);

    CHECK(dk_state_load(path, &s, &err) == 0);
    CHECK_STR(s.sel.preset, "work");
    CHECK_STR(dk_selection_override(&s.sel, "nvim"), "minimal");
    CHECK(s.nlinks == 2);
    /* ';', '=' and ':' inside paths must survive the round trip. */
    CHECK_STR(dk_state_find_link(&s, "/h/odd; path = x")->source, "/r/a: b");

    dk_state_remove_link(&s, "/h/.zshrc");
    CHECK(s.nlinks == 1);
    CHECK(dk_selection_clear_override(&s.sel, "nvim") == 1);
    CHECK(dk_selection_clear_override(&s.sel, "nvim") == 0);

    dk_state_add_link(&s, "/h/new\nline", "/r/x");
    CHECK(dk_state_save(path, &s, &err) != 0);
    dk_state_free(&s);
    free(path);
    free(dir);
}
