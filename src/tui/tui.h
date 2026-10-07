/*
 * tui.h: the full-screen interface, started with `dotkeeper --tui`.
 *
 * This is the only part of Dotkeeper that uses ncurses. Like the CLI it
 * only calls the core library; every change is shown as a plan and
 * applied only after confirmation.
 */
#ifndef DK_TUI_H
#define DK_TUI_H

#include "../cli/cli.h"

int dk_tui_run(const dk_opts *opts);

#endif
