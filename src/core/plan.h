/*
 * plan.h: turning a selection into file system changes.
 *
 * Every change goes through a plan. Building a plan only reads the file
 * system; it compares the links we want with what exists and with the
 * deployment record, and lists actions and conflicts. Nothing is touched
 * until the caller has seen the plan and decided to apply it. This gives
 * --dry-run and the TUI's preview for free and means a conflict is found
 * before anything has changed.
 */
#ifndef DK_PLAN_H
#define DK_PLAN_H

#include "state.h"

typedef enum {
    DK_ACT_REMOVE, /* delete our old symlink at path (it points at other) */
    DK_ACT_FORGET, /* drop path from the record; it is no longer our link */
    DK_ACT_BACKUP, /* move the file at path to other */
    DK_ACT_CREATE, /* create symlink path -> other */
    DK_ACT_ADOPT,  /* path already links to other; add it to the record */
} dk_act_kind;

typedef struct {
    dk_act_kind kind;
    char *path;
    char *other;
} dk_action;

typedef struct {
    char *path;
    char *reason;
} dk_conflict;

typedef struct {
    dk_action *acts;
    size_t nacts, cacts;
    dk_conflict *conflicts;
    size_t nconflicts, cconflicts;
} dk_plan;

typedef struct {
    const char *repo_root;
    const char *backup_dir; /* NULL: report files in the way as conflicts */
} dk_plan_opts;

/* The links the resolved choices ask for: one per file in each chosen
 * variant. Fails if two apps want the same target. */
int dk_wanted_links(const dk_choice *choices, size_t nchoices, dk_os os, dk_link **out,
                    size_t *nout, dk_err *err);
void dk_links_free(dk_link *links, size_t n);

int dk_plan_build(const dk_link *want, size_t nwant, const dk_state *state,
                  const dk_plan_opts *opts, dk_plan *out, dk_err *err);
/* The whole pipeline every front end needs: resolve state->sel, collect
 * the wanted links and build the plan. `skip_app` leaves one app (or every
 * app, for "*") undeployed; NULL deploys all. `backup_dir` as in
 * dk_plan_opts. */
int dk_plan_selection(const dk_repo *repo, const dk_state *state, dk_os os,
                      const char *skip_app, const char *backup_dir, dk_plan *out,
                      dk_err *err);
/* Applies actions in order and updates the record as each succeeds, so
 * the state stays accurate even if an action fails part way. */
int dk_plan_apply(const dk_plan *plan, dk_state *state, dk_err *err);
void dk_plan_free(dk_plan *plan);
const char *dk_act_name(dk_act_kind kind);

typedef enum {
    DK_HEALTH_OK,       /* the symlink is in place */
    DK_HEALTH_MISSING,  /* nothing at the target yet */
    DK_HEALTH_REPLACED, /* our link was replaced by a real file */
    DK_HEALTH_BLOCKED,  /* a file we never managed is in the way */
    DK_HEALTH_WRONG,    /* a symlink pointing somewhere else */
} dk_health;

dk_health dk_link_health(const dk_link *want, const dk_state *state);
const char *dk_health_name(dk_health h);

#endif
