#!/bin/bash
# Test suite for the `mapdrive` target.
#
# Unlike export-deps' stubbed svn, `subst` drive letters are real, global OS
# state that can't be sandboxed per test. So this suite finds whichever
# drive letters are actually free, uses a fresh one for every mapping (never
# reusing one within the same run -- deleting and immediately recreating the
# same letter is flaky and can fail with "Drive already SUBSTed"), and always
# unsubsts everything at the end via a trap, so a failing/interrupted run
# doesn't leave stray mappings on the dev machine.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORK_DIR="$(mktemp -d)"

PASS=0
FAIL=0
SUBSTED=()   # drive letters this run has subst'd, for cleanup

pass() { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

# A drive substed to a since-deleted folder still fails re-subst even
# though `-d` reports it as free, so check subst's own listing too.
is_free() {
    subst | grep -qi "^${1}:" && return 1
    [ -d "${1}:/" ] && return 1
    return 0
}

# Prints $1 free drive letters (one per line). Bottom-up from Z so it avoids
# common early letters. Never returns one already subst'd this run.
free_drives() {
    local need="$1" found=0 letter
    for letter in Z Y X W V U T S R Q P O N M L K J I H G F E D; do
        if is_free "$letter"; then
            echo "$letter"
            found=$((found+1))
            [ "$found" -ge "$need" ] && return 0
        fi
    done
    return 1
}

# Directly subst's a drive (bypassing the Makefile) to simulate one that's
# already in use, so tests can verify mapdrive skips it. Needs a native
# Windows path -- $WORK_DIR is POSIX-style and subst silently rejects it.
occupy_drive() {
    MSYS2_ARG_CONV_EXCL="*" subst "$1:" "$WIN_BASE" >/dev/null 2>&1
    SUBSTED+=("$1")
}

release_all() {
    local d
    for d in "${SUBSTED[@]}"; do
        MSYS2_ARG_CONV_EXCL="*" subst "$d:" /D >/dev/null 2>&1
    done
    SUBSTED=()
}
trap release_all EXIT

# Runs make and, on success, records whichever drive it mapped to (parsed
# from its own "-> X:" output) so release_all cleans it up too.
exec_make() {
    ( cd "$ROOT_DIR" && make -f "$ROOT_DIR/Makefile" "$@" ) \
        >"$WORK_DIR/stdout.log" 2>"$WORK_DIR/stderr.log"
    CODE=$?
    OUT="$(cat "$WORK_DIR/stdout.log")"
    ERR="$(cat "$WORK_DIR/stderr.log")"
    if [ "$CODE" -eq 0 ]; then
        local d
        d="$(printf '%s\n' "$OUT" | grep -oE -- '-> [A-Za-z]:' | tail -n1 | tr -dc 'A-Za-z')"
        [ -n "$d" ] && SUBSTED+=("$d")
    fi
}

# Fixture dirs, including one with a space and an '@' in the name.
mkdir -p "$WORK_DIR/plain_dir"
mkdir -p "$WORK_DIR/dir with space@2"
echo hi > "$WORK_DIR/dir with space@2/file.txt"

WIN_BASE="$(cd "$WORK_DIR" && pwd -W 2>/dev/null)"
[ -n "$WIN_BASE" ] || WIN_BASE="$WORK_DIR"
WIN_BASE="${WIN_BASE//\//\\}"
PLAIN_PATH="${WIN_BASE}\\plain_dir"
SPACE_AT_PATH="${WIN_BASE}\\dir with space@2"

# ---------------------------------------------------------------------------
# 1. Maps to the given free drive letter.
D1="$(free_drives 1)"
exec_make mapdrive MAP_PATH="$PLAIN_PATH" MAP_DRIVES="$D1"
mapping="$(subst | grep -i "^${D1}:")"
if [ "$CODE" -eq 0 ] && echo "$mapping" | grep -qF "plain_dir"; then
    pass "single_drive: maps to the given free drive"
else
    fail "single_drive: unexpected result (code=$CODE)"; echo "$OUT"; echo "$ERR"; echo "$mapping"
fi

# ---------------------------------------------------------------------------
# 2. Path with a space and '@' is passed through to `subst` intact.
D1="$(free_drives 1)"
exec_make mapdrive MAP_PATH="$SPACE_AT_PATH" MAP_DRIVES="$D1"
mapping="$(subst | grep -i "^${D1}:")"
if [ "$CODE" -eq 0 ] && echo "$mapping" | grep -qF "dir with space@2"; then
    pass "space_and_at: path with space and @ mapped correctly"
else
    fail "space_and_at: unexpected result (code=$CODE)"; echo "$OUT"; echo "$ERR"; echo "$mapping"
fi

# ---------------------------------------------------------------------------
# 3. Skips an already-occupied drive and uses the next free one.
D1="$(free_drives 1)"; occupy_drive "$D1"
D2="$(free_drives 1)"
exec_make mapdrive MAP_PATH="$PLAIN_PATH" MAP_DRIVES="${D1},${D2}"
mapping1="$(subst | grep -i "^${D1}:")"
mapping2="$(subst | grep -i "^${D2}:")"
if [ "$CODE" -eq 0 ] && ! echo "$mapping1" | grep -qF "plain_dir" && echo "$mapping2" | grep -qF "plain_dir"; then
    pass "skip_occupied: skips the busy drive, maps to the next free one"
else
    fail "skip_occupied: unexpected result (code=$CODE)"; echo "$OUT"; echo "$ERR"; echo "$mapping1"; echo "$mapping2"
fi

# ---------------------------------------------------------------------------
# 4. MAP_DRIVES with stray spaces and colons around entries is trimmed.
D1="$(free_drives 1)"; occupy_drive "$D1"
D2="$(free_drives 1)"
exec_make mapdrive MAP_PATH="$PLAIN_PATH" MAP_DRIVES=" ${D1}: , ${D2} "
mapping2="$(subst | grep -i "^${D2}:")"
if [ "$CODE" -eq 0 ] && echo "$mapping2" | grep -qF "plain_dir"; then
    pass "messy_csv: whitespace/colons around drive entries are trimmed"
else
    fail "messy_csv: unexpected result (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 5. Missing MAP_PATH errors out with a clear message and non-zero exit.
D1="$(free_drives 1)"
exec_make mapdrive MAP_DRIVES="$D1"
if [ "$CODE" -ne 0 ] && echo "$ERR" | grep -q "MAP_PATH is required"; then
    pass "missing_path: errors with non-zero exit and clear message"
else
    fail "missing_path: expected error was not raised (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 6. Missing MAP_DRIVES errors out with a clear message and non-zero exit.
exec_make mapdrive MAP_PATH="$PLAIN_PATH"
if [ "$CODE" -ne 0 ] && echo "$ERR" | grep -q "MAP_DRIVES is required"; then
    pass "missing_drives: errors with non-zero exit and clear message"
else
    fail "missing_drives: expected error was not raised (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 7. No drive available among MAP_DRIVES errors out clearly.
D1="$(free_drives 1)"; occupy_drive "$D1"
exec_make mapdrive MAP_PATH="$PLAIN_PATH" MAP_DRIVES="$D1"
if [ "$CODE" -ne 0 ] && echo "$ERR" | grep -q "No available drive letter"; then
    pass "no_drive_available: errors clearly when every candidate is busy"
else
    fail "no_drive_available: expected error was not raised (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
echo
echo "Results: $PASS passed, $FAIL failed"
release_all
rm -rf "$WORK_DIR"
[ "$FAIL" -eq 0 ]
