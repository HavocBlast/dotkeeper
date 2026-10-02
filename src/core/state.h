/*
 * state.h: what Dotkeeper has done on this machine.
 *
 * Stored in $XDG_STATE_HOME/dotkeeper/state.ini, never in the repo,
 * because each machine has its own selection. The link list is the
 * deployment record: Dotkeeper only ever removes links listed here.
 */
#ifndef DK_STATE_H
#define DK_STATE_H

#include "selection.h"

typedef struct {
    char *target; /* the symlink's path, e.g. /home/u/.zshrc */
    char *source; /* the file in the repo it points at */
} dk_link;

typedef struct {
    dk_selection sel;
    dk_link *links;
    size_t nlinks, clinks;
} dk_state;

char *dk_state_path(void);
/* A new, not yet created, timestamped directory for --backup. */
char *dk_new_backup_dir(void);
/* A missing file loads as an empty state. */
int dk_state_load(const char *path, dk_state *out, dk_err *err);
int dk_state_save(const char *path, const dk_state *state, dk_err *err);
void dk_state_free(dk_state *state);

const dk_link *dk_state_find_link(const dk_state *state, const char *target);
void dk_state_add_link(dk_state *state, const char *target, const char *source);
void dk_state_remove_link(dk_state *state, const char *target);

#endif
