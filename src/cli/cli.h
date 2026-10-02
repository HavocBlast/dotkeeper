/*
 * cli.h: shared pieces of the command-line front end.
 *
 * The front end turns arguments into calls to the core library and turns
 * results and errors into messages. It holds no dotfile logic itself, so
 * the TUI can make the same calls.
 */
#ifndef DK_CLI_H
#define DK_CLI_H

#include "../core/plan.h"
#include "../core/repo.h"
#include "../core/state.h"

#define DK_VERSION "0.1.0-dev"

enum { DK_EXIT_OK = 0, DK_EXIT_ERROR = 1, DK_EXIT_USAGE = 2 };

typedef struct {
    const char *repo; /* --repo */
    int dry_run;
    int backup;
    int verbose;
    char **args; /* arguments after the subcommand, sub-flags included */
    int nargs;
} dk_opts;

/* Everything a command needs: the repo, this machine's state, the OS. */
typedef struct {
    char *repo_path;
    dk_repo repo;
    char *state_path;
    dk_state state;
    dk_os os;
} dk_ctx;

void dk_error(const char *fmt, ...) DK_PRINTF(1, 2);
void dk_warn(const char *fmt, ...) DK_PRINTF(1, 2);

/* Finds the repo (--repo, config, or by asking when interactive), loads
 * it and the state. Prints its own errors. */
int ctx_open(const dk_opts *opts, dk_ctx *ctx);
void ctx_close(dk_ctx *ctx);

/* Makes the file system match ctx->state.sel: plans, shows the plan,
 * applies it and saves the state (all skipped with --dry-run except
 * showing the plan). `skip_app`, when set, is left undeployed; "*" means
 * every app. Returns an exit code. */
int sync_links(dk_ctx *ctx, const dk_opts *opts, const char *skip_app);

/* Asks for the repo location on the terminal. Returns NULL if declined. */
char *prompt_repo_path(void);

int cmd_init(const dk_opts *opts);
int cmd_status(const dk_opts *opts);
int cmd_list(const dk_opts *opts);
int cmd_use(const dk_opts *opts);
int cmd_preset(const dk_opts *opts);
int cmd_deploy(const dk_opts *opts);
int cmd_undeploy(const dk_opts *opts);

#endif
