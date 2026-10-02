#include "test.h"

#include <stdlib.h>
#include <sys/stat.h>

#include "../../src/core/selection.h"
#include "../../src/platform/paths.h"

static char *make_repo(void)
{
    dk_err err;
    char *root = test_tmpdir();
    CHECK(dk_repo_create(root, &err) == 0);
    test_write(root, "apps/zsh/default/.zshrc", "a\n");
    test_write(root, "apps/zsh/work/.zshrc", "w\n");
    test_write(root, "apps/nvim/minimal/.config/nvim/init.lua", "m\n");
    test_write(root, "apps/mac/app.ini", "[app]\nos = macos\n[packages]\nbrew = x\n");
    test_write(root, "apps/mac/default/.macrc", "x\n");
    test_write(root, "presets/work.ini", "[preset]\ndescription = Work\n[apps]\nzsh = work\n");
    test_write(root, "presets/broken.ini", "[apps]\nzsh = nope\n");
    return root;
}

static const dk_choice *find_choice(const dk_choice *c, size_t n, const char *app)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(c[i].app->name, app) == 0)
            return &c[i];
    return NULL;
}

void test_repo(void)
{
    dk_err err;
    char *root = make_repo();
    dk_repo repo;
    if (dk_repo_load(root, &repo, &err) != 0) {
        fprintf(stderr, "load: %s\n", err.msg);
        test_failures++;
        return;
    }
    CHECK(repo.napps == 3);
    CHECK(repo.npresets == 2);
    CHECK(repo.warnings.len == 0); /* [packages] is reserved, not unknown */
    const dk_app *mac = dk_repo_find_app(&repo, "mac");
    CHECK(mac && mac->os_mask == DK_OS_MACOS);
    CHECK_STR(dk_repo_find_preset(&repo, "work")->description, "Work");

    /* Default selection: zsh uses default; nvim has no default variant. */
    dk_selection sel = {0};
    dk_choice *c;
    size_t n;
    CHECK(dk_resolve(&repo, &sel, DK_OS_LINUX, &c, &n, &err) == 0);
    CHECK(n == 2); /* "mac" is macOS-only */
    CHECK_STR(find_choice(c, n, "zsh")->variant->name, "default");
    CHECK(find_choice(c, n, "nvim")->variant == NULL);
    free(c);

    /* Preset, then an override on top. */
    dk_selection_set_preset(&sel, "work");
    dk_selection_set_override(&sel, "nvim", "minimal");
    CHECK(dk_resolve(&repo, &sel, DK_OS_LINUX, &c, &n, &err) == 0);
    CHECK_STR(find_choice(c, n, "zsh")->variant->name, "work");
    CHECK(find_choice(c, n, "zsh")->origin == DK_FROM_PRESET);
    CHECK(find_choice(c, n, "nvim")->origin == DK_FROM_OVERRIDE);
    free(c);

    /* A preset naming a missing variant is an error, not a silent default. */
    dk_selection_set_preset(&sel, "broken");
    CHECK(dk_resolve(&repo, &sel, DK_OS_LINUX, &c, &n, &err) != 0);
    dk_selection_set_preset(&sel, "nope");
    CHECK(dk_resolve(&repo, &sel, DK_OS_LINUX, &c, &n, &err) != 0);
    dk_selection_free(&sel);
    dk_repo_free(&repo);

    /* Variants that differ only by case collide on macOS: reject them. */
    test_write(root, "apps/zsh/Work/.zshrc", "x\n");
    CHECK(dk_repo_load(root, &repo, &err) != 0);
    free(root);

    /* A directory without dotkeeper.ini is not a repo. */
    char *empty = test_tmpdir();
    CHECK(dk_repo_load(empty, &repo, &err) != 0);
    free(empty);
}
