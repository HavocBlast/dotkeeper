#!/bin/sh
# Integration tests: run the real binary against a throwaway $HOME and
# check the links it leaves behind. POSIX sh, so it runs on macOS too.
set -u

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/dotkeeper-it.XXXXXX")
WORK=$(cd "$WORK" && pwd -P) # /tmp is a symlink on macOS
trap 'rm -rf "$WORK"' EXIT

export HOME="$WORK/home"
unset XDG_CONFIG_HOME XDG_STATE_HOME
mkdir -p "$HOME"
REPO="$HOME/dots"
FAILED=0

fail() {
    echo "FAIL: $*" >&2
    FAILED=$((FAILED + 1))
}

dk() {
    "$BIN" "$@" </dev/null >"$WORK/out" 2>"$WORK/err"
}

expect_ok() {
    dk "$@" || fail "dotkeeper $* exited $?: $(cat "$WORK/err")"
}

expect_fail() {
    if dk "$@"; then fail "dotkeeper $* should have failed"; fi
}

expect_link() { # path target
    got=$(readlink "$1" 2>/dev/null) || got="(not a link)"
    [ "$got" = "$2" ] || fail "$1 -> $got, expected $2"
}

expect_file() { # path content
    if [ -L "$1" ] || [ ! -f "$1" ]; then
        fail "$1 should be a regular file"
    elif [ "$(cat "$1")" != "$2" ]; then
        fail "$1 has the wrong content"
    fi
}

expect_absent() {
    if [ -e "$1" ] || [ -L "$1" ]; then fail "$1 should not exist"; fi
}

expect_out() { # pattern
    grep -q -- "$1" "$WORK/out" || fail "output lacks \"$1\": $(cat "$WORK/out")"
}

put() { # path content
    mkdir -p "$(dirname "$1")"
    printf '%s\n' "$2" >"$1"
}

# --- no repo yet, not interactive: a clear error
expect_fail status
grep -q "dotkeeper init" "$WORK/err" || fail "missing hint to run init"

# --- init creates the skeleton and remembers the path
expect_ok init "$REPO"
[ -f "$REPO/dotkeeper.ini" ] || fail "init did not create dotkeeper.ini"
[ -d "$REPO/apps" ] && [ -d "$REPO/presets" ] || fail "init did not create apps/ presets/"
grep -q "repo = $REPO" "$HOME/.config/dotkeeper/config.ini" || fail "config not saved"

put "$REPO/apps/zsh/default/.zshrc" "zsh default"
put "$REPO/apps/zsh/work/.zshrc" "zsh work"
put "$REPO/apps/nvim/default/.config/nvim/init.lua" "nvim default"
put "$REPO/apps/nvim/minimal/.config/nvim/init.lua" "nvim minimal"
put "$REPO/apps/nvim/minimal/.config/nvim/lua/extra.lua" "extra"
printf '[preset]\ndescription = Work laptop\n\n[apps]\nzsh = work\nnvim = minimal\n' \
    >"$REPO/presets/work.ini"

# --- dry run changes nothing
expect_ok deploy --dry-run
expect_out "Would do"
expect_absent "$HOME/.zshrc"

# --- deploy links every file of the default variants
expect_ok deploy
expect_link "$HOME/.zshrc" "$REPO/apps/zsh/default/.zshrc"
expect_link "$HOME/.config/nvim/init.lua" "$REPO/apps/nvim/default/.config/nvim/init.lua"
expect_ok status
expect_out "zsh .*default .*1 ok"

# --- per-app switch
expect_ok use nvim minimal
expect_link "$HOME/.config/nvim/init.lua" "$REPO/apps/nvim/minimal/.config/nvim/init.lua"
expect_link "$HOME/.config/nvim/lua/extra.lua" "$REPO/apps/nvim/minimal/.config/nvim/lua/extra.lua"
expect_ok status
expect_out "nvim .*minimal .*override"

# --- reset goes back to default and removes the extra file's link
expect_ok use nvim --reset
expect_link "$HOME/.config/nvim/init.lua" "$REPO/apps/nvim/default/.config/nvim/init.lua"
expect_absent "$HOME/.config/nvim/lua/extra.lua"

# --- bad names are rejected without changes
expect_fail use nvim nope
expect_fail use nope default
expect_fail preset apply nope

# --- preset switches everything and clears overrides
expect_ok use zsh default
expect_ok preset apply work
expect_link "$HOME/.zshrc" "$REPO/apps/zsh/work/.zshrc"
expect_link "$HOME/.config/nvim/init.lua" "$REPO/apps/nvim/minimal/.config/nvim/init.lua"
expect_ok list presets
expect_out "^\* work"

# --- edits through the link land in the repo
printf 'edited\n' >"$HOME/.zshrc"
[ "$(cat "$REPO/apps/zsh/work/.zshrc")" = "edited" ] || fail "edit did not reach the repo"

# --- a file in the way is a conflict; nothing changes without --backup
expect_ok preset clear
rm "$HOME/.zshrc"
put "$HOME/.zshrc" "precious"
expect_fail deploy
expect_file "$HOME/.zshrc" "precious"
expect_ok deploy --backup
expect_link "$HOME/.zshrc" "$REPO/apps/zsh/default/.zshrc"
found=$(find "$HOME/.local/state/dotkeeper/backups" -name .zshrc -type f)
[ -n "$found" ] && [ "$(cat "$found")" = "precious" ] || fail "backup not found"

# --- a link Dotkeeper did not create is never removed
rm "$HOME/.zshrc"
ln -s /etc/hosts "$HOME/.zshrc"
expect_fail deploy
expect_link "$HOME/.zshrc" /etc/hosts
rm "$HOME/.zshrc"

# --- an app whose file vanished from the repo loses its link
expect_ok deploy
rm "$REPO/apps/nvim/default/.config/nvim/init.lua"
expect_ok deploy
expect_absent "$HOME/.config/nvim/init.lua"

# --- preset save writes what is active
expect_ok use zsh work
expect_ok preset save mine
grep -q "zsh = work" "$REPO/presets/mine.ini" || fail "preset save missed zsh"
expect_fail preset save mine

# --- undeploy removes only Dotkeeper's links
put "$HOME/.bashrc" "not managed"
expect_ok undeploy
expect_absent "$HOME/.zshrc"
expect_file "$HOME/.bashrc" "not managed"
[ -f "$REPO/apps/zsh/work/.zshrc" ] || fail "undeploy touched the repo"

# --- OS filter: an app for the other OS is ignored
case "$(uname -s)" in
Darwin) other=linux ;;
*) other=macos ;;
esac
put "$REPO/apps/only/app.ini" "[app]
os = $other"
put "$REPO/apps/only/default/.onlyrc" "x"
expect_ok deploy
expect_absent "$HOME/.onlyrc"

# --- usage errors
expect_fail frobnicate
expect_fail status --bogus
expect_fail --tui

if [ "$FAILED" -ne 0 ]; then
    echo "$FAILED integration check(s) failed" >&2
    exit 1
fi
echo "integration tests passed"
