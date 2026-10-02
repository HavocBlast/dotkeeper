# Dotkeeper

Dotkeeper manages dotfiles kept in a git repo. It links each file from the
repo into your home directory, and lets you switch between versions of an
app's config (variants) one app at a time or all at once (presets).

An application manager (installing the apps themselves) is planned; the
code leaves room for it but does not implement it yet.

Status: early development. Both the command-line interface and the
full-screen interface (`dotkeeper --tui`, built on ncurses) work.

## Build

Needs a C compiler, GNU make and ncurses. Tested on Arch Linux, Debian
and macOS.

- Arch: `pacman -S base-devel ncurses`
- Debian: `apt install build-essential libncurses-dev pkg-config`
- macOS: the Xcode command line tools (ncurses ships with macOS)

```sh
make
make test              # unit and integration tests
make clean && make SANITIZE=1 test   # with AddressSanitizer + UBSan
make install PREFIX=~/.local
```

## Your dotfile repo

```
~/.dotfiles/
  dotkeeper.ini                       # created by `dotkeeper init`
  apps/
    zsh/
      default/.zshrc                  # -> ~/.zshrc
      work/.zshrc                     # another variant of the same file
    nvim/
      app.ini                         # optional settings, see below
      default/.config/nvim/init.lua   # -> ~/.config/nvim/init.lua
  presets/
    work.ini
```

Each variant directory mirrors your home directory. Every file in the
chosen variant becomes a symlink at the same path under `$HOME`, so you
edit files in place and the changes land in the repo.

A preset picks a variant for several apps. Apps it does not list use
their `default` variant:

```ini
# presets/work.ini
[preset]
description = Work laptop

[apps]
zsh = work
nvim = minimal
```

`app.ini` is optional:

```ini
[app]
os = linux, macos            # skip the app on other systems

[target]
root.macos = ~/Library/Application Support   # where the variant maps to

[packages]                   # reserved for the application manager
```

## Commands

```
dotkeeper init [path]          Set (and create) the dotfile repo; asks if no path
dotkeeper status [-v]          Active preset, each app's variant, link health
dotkeeper list [apps|presets]  What the repo contains
dotkeeper list variants <app>
dotkeeper use <app> <variant>  Switch one app
dotkeeper use <app> --reset    Back to the preset's choice
dotkeeper preset apply <name>  Switch every app
dotkeeper preset save <name>   Save the current choices as a preset
dotkeeper preset clear         Stop using a preset
dotkeeper deploy               Create missing links (e.g. after git pull)
dotkeeper undeploy [app]       Remove Dotkeeper's links
```

Options: `--repo <path>`, `-n/--dry-run`, `--backup`, `-v/--verbose`.

### Full-screen interface

`dotkeeper --tui` shows your apps on the left and the selected app's
variants on the right. Up/Down (or j/k) move, Tab switches panes, Enter
picks a variant, `r` resets an app to its preset's variant, `p` opens the
preset list, `d` deploys, `q` quits. Every change shows its plan first and
is only applied after you confirm; with `--dry-run` nothing is applied.

### Safety rules

- Dotkeeper only removes symlinks it created itself. It keeps a record of
  them in `~/.local/state/dotkeeper/state.ini`.
- If a real file is where a link should go, nothing is changed and the
  conflict is reported. `--backup` moves such files to
  `~/.local/state/dotkeeper/backups/<time>/` first.
- Every change is planned before anything is touched; `--dry-run` shows the
  plan.

## Code layout

```
src/main.c      argument parsing and dispatch
src/cli/        the commands; turns results into output
src/tui/        the ncurses interface (the only code that uses ncurses)
src/core/       repo model, selection, state, planner (no terminal output)
src/platform/   the only OS-specific code: paths, file system, OS detection
src/util/       allocation, strings, errors
vendor/inih/    INI parser (BSD-3-Clause)
tests/          unit tests and shell integration tests
```
