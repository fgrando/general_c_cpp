#!/bin/bash
# Test suite for the deps.txt-parsing Makefile.
# Uses a stub `svn` on PATH so no real network/repo access is needed.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"
WORK_DIR="$(mktemp -d)"
STUB_DIR="$WORK_DIR/stub"
mkdir -p "$STUB_DIR"

cat > "$STUB_DIR/svn" <<'EOS'
#!/bin/bash
echo "SVN_CALL $*" >> "$SVN_LOG"
EOS
chmod +x "$STUB_DIR/svn"

# Some environments (e.g. Cygwin with a "textmode" mount) silently rewrite
# every '\n' written via '>>' into '\r\n' on disk. That would add a trailing
# \r to EVERY log line regardless of the Makefile's own CR-stripping, so
# detect it once here and account for it in the crlf test below instead of
# assuming a bare \r in the log always means the parser is broken.
AMBIENT_CRLF=0
printf 'probe' > "$WORK_DIR/crlf_probe.txt"
echo "x" >> "$WORK_DIR/crlf_probe.txt"
if grep -q "$(printf '\r')" "$WORK_DIR/crlf_probe.txt"; then
    AMBIENT_CRLF=1
fi

# Counts consecutive trailing \r characters in $1.
count_trailing_cr() {
    local s="$1" n=0
    while [ "${s: -1}" = "$(printf '\r')" ]; do
        n=$((n+1))
        s="${s%?}"
    done
    echo "$n"
}

PASS=0
FAIL=0
SANDBOX_COUNT=0

pass() { echo "PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }

new_sandbox() {
    SANDBOX_COUNT=$((SANDBOX_COUNT+1))
    SANDBOX="$WORK_DIR/sandbox_$SANDBOX_COUNT"
    mkdir -p "$SANDBOX"
}

# Runs `make` inside $SANDBOX with the given extra args (e.g. DEPS_FILE=... FORCE=1).
# Sets CODE, OUT, ERR, SVN_LOG (path to the log of stub svn invocations).
exec_make() {
    SVN_LOG="$SANDBOX/svn.log"
    : > "$SVN_LOG"
    export SVN_LOG
    ( cd "$SANDBOX" && PATH="$STUB_DIR:$PATH" make -f "$ROOT_DIR/Makefile" "$@" ) \
        >"$SANDBOX/stdout.log" 2>"$SANDBOX/stderr.log"
    CODE=$?
    OUT="$(cat "$SANDBOX/stdout.log")"
    ERR="$(cat "$SANDBOX/stderr.log")"
}

svn_call_count() { grep -c '^SVN_CALL' "$SVN_LOG" 2>/dev/null || true; }

# ---------------------------------------------------------------------------
# 1. Basic parsing: 3 valid rows, one comment, one blank line.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/basic.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 3 ] \
    && grep -qF "SVN_CALL export --force -r 1234 http://192.168.1.228/svn/myapp/trunk/calc calculator/version1" "$SVN_LOG" \
    && grep -qF "SVN_CALL export --force -r 1000 http://192.168.1.228/svn/myapp/trunk/clock clock" "$SVN_LOG" \
    && grep -qF "SVN_CALL export --force -r 2001 http://192.168.1.228/svn/editor/tag/v1.0 tools/editor.exe" "$SVN_LOG"; then
    pass "basic: parses 3 valid rows, skips comment and blank line"
else
    fail "basic: unexpected output (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 2. Trailing newline is NOT required at end of file.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/no_trailing_newline.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 3 ] \
    && grep -qF "SVN_CALL export --force -r 2001 http://192.168.1.228/svn/editor/tag/v1.0 tools/editor.exe" "$SVN_LOG"; then
    pass "no_trailing_newline: last row (no trailing \\n) is still processed"
else
    fail "no_trailing_newline: last row without trailing newline was NOT processed (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 3. CRLF line endings don't leak \r into parsed fields.
# Compares the trailing-\r count on the last field against AMBIENT_CRLF
# (0 normally, 1 on environments that add their own \r to every log line)
# so this only fails on a \r actually introduced by the Makefile's parsing,
# not one contributed by the test harness's own file writes.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/crlf.deps"
last_field="$(grep '^SVN_CALL' "$SVN_LOG" | tail -n1)"
last_field="${last_field##* }"
cr_count="$(count_trailing_cr "$last_field")"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 2 ] && [ "$cr_count" -le "$AMBIENT_CRLF" ] \
    && grep -qF "SVN_CALL export --force -r 1 http://example.com/svn/calc out/calc" "$SVN_LOG" \
    && grep -qF "SVN_CALL export --force -r 2 http://example.com/svn/clock out/clock" "$SVN_LOG"; then
    pass "crlf: CRLF-terminated lines parsed cleanly, no stray \\r"
else
    fail "crlf: CRLF handling broken (code=$CODE, trailing CRs on dest=$cr_count, ambient=$AMBIENT_CRLF)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 4. Irregular whitespace (tabs, multiple spaces, leading/trailing spaces) is trimmed.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/whitespace.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 2 ] \
    && grep -qF "SVN_CALL export --force -r 5 http://example.com/svn/calc out/calc" "$SVN_LOG" \
    && grep -qF "SVN_CALL export --force -r 6 http://example.com/svn/clock out/clock" "$SVN_LOG"; then
    pass "whitespace: tabs/extra spaces/leading-trailing spaces trimmed correctly"
else
    fail "whitespace: fields not trimmed correctly (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 5. Consecutive blank lines and whitespace-only lines are all skipped.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/blank_lines.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 2 ]; then
    pass "blank_lines: multiple blank/whitespace-only lines skipped"
else
    fail "blank_lines: expected 2 svn calls, got $(svn_call_count) (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 6. Malformed row (missing columns) is skipped with a warning; other rows still run.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/malformed.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 2 ] && echo "$ERR" | grep -q "Skipping malformed line"; then
    pass "malformed: bad row skipped with warning, valid rows still processed"
else
    fail "malformed: did not handle bad row as expected (code=$CODE, calls=$(svn_call_count))"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 7. Missing deps file errors out with a clear message and non-zero exit.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/does_not_exist.deps"
if [ "$CODE" -ne 0 ] && echo "$ERR" | grep -q "Deps file not found"; then
    pass "missing_file: errors with non-zero exit and clear message"
else
    fail "missing_file: expected error was not raised (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 8. Existing destination is skipped unless FORCE=1.
new_sandbox
mkdir -p "$SANDBOX/out"
: > "$SANDBOX/out/foo"
exec_make DEPS_FILE="$FIXTURES_DIR/single.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 0 ] && echo "$OUT" | grep -q "already exists, skipping"; then
    pass "existing_dest: skips export when destination already exists"
else
    fail "existing_dest: expected skip did not happen (code=$CODE, calls=$(svn_call_count))"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 9. FORCE=1 overwrites an existing destination.
new_sandbox
mkdir -p "$SANDBOX/out"
: > "$SANDBOX/out/foo"
exec_make DEPS_FILE="$FIXTURES_DIR/single.deps" FORCE=1
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 1 ]; then
    pass "existing_dest_force: FORCE=1 re-exports over existing destination"
else
    fail "existing_dest_force: expected 1 svn call, got $(svn_call_count) (code=$CODE)"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
# 10. File with only comments/blank lines: no calls, clean exit, no false warnings.
new_sandbox
exec_make DEPS_FILE="$FIXTURES_DIR/only_comments.deps"
if [ "$CODE" -eq 0 ] && [ "$(svn_call_count)" -eq 0 ] && ! echo "$ERR" | grep -q "Skipping malformed line"; then
    pass "only_comments: no exports run, no false malformed warnings"
else
    fail "only_comments: unexpected output (code=$CODE, calls=$(svn_call_count))"; echo "$OUT"; echo "$ERR"
fi

# ---------------------------------------------------------------------------
echo
echo "Results: $PASS passed, $FAIL failed"
rm -rf "$WORK_DIR"
[ "$FAIL" -eq 0 ]
