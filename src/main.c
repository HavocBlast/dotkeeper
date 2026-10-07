/*
 * main.c: argument parsing and subcommand dispatch.
 *
 * Global options may appear anywhere on the command line. Anything else
 * that starts with '-' is passed to the subcommand, which rejects what it
 * does not understand. This is hand-written rather than getopt_long
 * because GNU and BSD getopt differ in how they reorder arguments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/cli.h"
#include "tui/tui.h"

static void usage(FILE *out)
{
    fputs("Usage: dotkeeper [options] <command> [args]\n"
          "\n"
          "Commands:\n"
          "  init [path]                Set (and create) the dotfile repo\n"
          "  status                     Show the active preset, variants and links\n"
          "  list [apps|presets]        List what the repo contains\n"
          "  list variants <app>        List an app's variants\n"
          "  use <app> <variant>        Switch one app to a variant\n"
          "  use <app> --reset          Go back to the preset's variant\n"
          "  preset apply <name>        Switch every app to a preset\n"
          "  preset save <name>         Save the current selection as a preset\n"
          "  preset clear               Stop using a preset (all apps use default)\n"
          "  deploy                     Create missing links for the current selection\n"
          "  undeploy [app]             Remove Dotkeeper's links\n"
          "\n"
          "Options:\n"
          "  --repo <path>              Use this repo instead of the configured one\n"
          "  -n, --dry-run              Show what would change without changing it\n"
          "      --backup               Move files that are in the way aside first\n"
          "  -v, --verbose              Show each link\n"
          "      --tui                  Open the full-screen interface\n"
          "  -h, --help                 Show this help\n"
          "      --version              Show the version\n",
          out);
}

typedef struct {
    const char *name;
    int (*run)(const dk_opts *);
} command;

static const command commands[] = {
    {"init", cmd_init},       {"status", cmd_status}, {"list", cmd_list},
    {"use", cmd_use},         {"preset", cmd_preset}, {"deploy", cmd_deploy},
    {"undeploy", cmd_undeploy},
};

int main(int argc, char **argv)
{
    dk_opts opts = {0};
    int tui = 0;
    const char *cmd = NULL;
    char **rest = calloc((size_t)argc, sizeof *rest);
    if (!rest)
        return DK_EXIT_ERROR;

    int only_args = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (only_args || a[0] != '-' || a[1] == '\0') {
            if (!cmd)
                cmd = a;
            else
                rest[opts.nargs++] = argv[i];
        } else if (strcmp(a, "--") == 0) {
            only_args = 1;
        } else if (strcmp(a, "--repo") == 0) {
            if (++i == argc) {
                dk_error("--repo needs a path");
                free(rest);
                return DK_EXIT_USAGE;
            }
            opts.repo = argv[i];
        } else if (strncmp(a, "--repo=", 7) == 0) {
            opts.repo = a + 7;
        } else if (strcmp(a, "-n") == 0 || strcmp(a, "--dry-run") == 0) {
            opts.dry_run = 1;
        } else if (strcmp(a, "--backup") == 0) {
            opts.backup = 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            opts.verbose = 1;
        } else if (strcmp(a, "--tui") == 0) {
            tui = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            free(rest);
            return DK_EXIT_OK;
        } else if (strcmp(a, "--version") == 0) {
            printf("dotkeeper %s\n", DK_VERSION);
            free(rest);
            return DK_EXIT_OK;
        } else if (cmd) {
            rest[opts.nargs++] = argv[i]; /* a subcommand's own flag */
        } else {
            dk_error("unknown option %s (see `dotkeeper --help`)", a);
            free(rest);
            return DK_EXIT_USAGE;
        }
    }
    opts.args = rest;

    int code;
    if (tui && cmd) {
        dk_error("--tui does not take a command");
        code = DK_EXIT_USAGE;
    } else if (tui) {
        code = dk_tui_run(&opts);
    } else if (!cmd) {
        usage(stderr);
        code = DK_EXIT_USAGE;
    } else {
        code = -1;
        for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++)
            if (strcmp(cmd, commands[i].name) == 0)
                code = commands[i].run(&opts);
        if (code == -1) {
            dk_error("unknown command \"%s\" (see `dotkeeper --help`)", cmd);
            code = DK_EXIT_USAGE;
        }
    }
    free(rest);
    return code;
}
