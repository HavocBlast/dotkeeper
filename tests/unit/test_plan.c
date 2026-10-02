#include "test.h"

#include <stdlib.h>
#include <unistd.h>

#include "../../src/core/plan.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/paths.h"

static size_t count(const dk_plan *p, dk_act_kind kind)
{
    size_t n = 0;
    for (size_t i = 0; i < p->nacts; i++)
        n += p->acts[i].kind == kind;
    return n;
}

void test_plan(void)
{
    dk_err err;
    char *repo = test_tmpdir();
    char *home = test_tmpdir();
    test_write(repo, "a", "a\n");
    test_write(repo, "b", "b\n");
    char *src_a = dk_path_join(repo, "a"), *src_b = dk_path_join(repo, "b");
    char *dst = dk_path_join(home, "sub/.rc");
    dk_link want_a = {dst, src_a}, want_b = {dst, src_b};
    dk_plan_opts opts = {repo, NULL};
    dk_state s = {0};
    dk_plan p;

    /* Nothing there yet: one link. */
    CHECK(dk_plan_build(&want_a, 1, &s, &opts, &p, &err) == 0);
    CHECK(p.nacts == 1 && count(&p, DK_ACT_CREATE) == 1 && p.nconflicts == 0);
    CHECK(dk_plan_apply(&p, &s, &err) == 0);
    dk_plan_free(&p);
    CHECK(dk_link_health(&want_a, &s) == DK_HEALTH_OK);

    /* Same again: nothing to do. */
    CHECK(dk_plan_build(&want_a, 1, &s, &opts, &p, &err) == 0);
    CHECK(p.nacts == 0);
    dk_plan_free(&p);

    /* Switch variant: remove our old link, create the new one. */
    CHECK(dk_plan_build(&want_b, 1, &s, &opts, &p, &err) == 0);
    CHECK(count(&p, DK_ACT_REMOVE) == 1 && count(&p, DK_ACT_CREATE) == 1);
    CHECK(dk_plan_apply(&p, &s, &err) == 0);
    dk_plan_free(&p);
    char *rl = dk_readlink(dst);
    CHECK_STR(rl, src_b);
    free(rl);

    /* A real file replaced our link: report it, never delete it. */
    unlink(dst);
    test_write(home, "sub/.rc", "user edit\n");
    CHECK(dk_link_health(&want_b, &s) == DK_HEALTH_REPLACED);
    CHECK(dk_plan_build(&want_b, 1, &s, &opts, &p, &err) == 0);
    CHECK(p.nconflicts == 1 && p.nacts == 0);
    dk_plan_free(&p);

    /* With a backup directory the file is moved aside first. */
    char *backups = dk_path_join(home, "backups");
    opts.backup_dir = backups;
    CHECK(dk_plan_build(&want_b, 1, &s, &opts, &p, &err) == 0);
    CHECK(p.nconflicts == 0 && count(&p, DK_ACT_BACKUP) == 1);
    CHECK(dk_plan_apply(&p, &s, &err) == 0);
    dk_plan_free(&p);
    /* dst is outside $HOME here, so its full path is kept under backups. */
    char *moved = dk_path_join(backups, dst);
    CHECK(dk_fs_kind_of(moved) == DK_FS_FILE);
    opts.backup_dir = NULL;

    /* No longer wanted: our link is removed, and only ours. */
    CHECK(dk_plan_build(NULL, 0, &s, &opts, &p, &err) == 0);
    CHECK(count(&p, DK_ACT_REMOVE) == 1);
    CHECK(dk_plan_apply(&p, &s, &err) == 0);
    dk_plan_free(&p);
    CHECK(dk_fs_kind_of(dst) == DK_FS_MISSING);
    CHECK(s.nlinks == 0);

    /* A target whose directory is a symlink into the repo is refused,
     * even with --backup, because "backing it up" would move repo files. */
    char *linked_dir = dk_path_join(home, "linked");
    CHECK(symlink(repo, linked_dir) == 0);
    char *inside = dk_path_join(linked_dir, "a");
    dk_link want_in = {inside, src_a};
    opts.backup_dir = backups;
    CHECK(dk_plan_build(&want_in, 1, &s, &opts, &p, &err) == 0);
    CHECK(p.nconflicts == 1 && p.nacts == 0);
    dk_plan_free(&p);

    dk_state_free(&s);
    free(inside);
    free(linked_dir);
    free(moved);
    free(backups);
    free(src_a);
    free(src_b);
    free(dst);
    free(repo);
    free(home);
}
