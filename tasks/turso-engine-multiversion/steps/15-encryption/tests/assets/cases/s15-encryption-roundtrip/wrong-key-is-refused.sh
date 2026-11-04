# R15.7: an encrypted database opened with the wrong key, or with no key at
# all, fails outright instead of returning garbage. Two things are asserted
# per attempt: not one row of the encrypted table reaches standard output, and
# the process's exit status is non-zero. The status is folded to `zero` or
# `nonzero` -- which non-zero status the failure carries is not part of the
# contract, only that the process failed.
#
# There is no second opinion available for this case. Stock sqlite3 has no
# concept of a key, and stock sqlite3 cannot open an encrypted database at
# all, so it can say nothing about a wrong key being refused or about the exit
# status that carries the refusal. What stock sqlite3 does make of such a file
# is itself a graded requirement of this step (R15.8), and it is asserted by
# the sibling case stock-sqlite3-rejects-encrypted-file, which invokes stock
# sqlite3 directly.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
other=ffeeddccbbaa99887766554433221100101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'guarded one'),(2,'guarded two');
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL

# attempt <label> <prologue-sql>
attempt() {
    local label="$1" prologue="$2" out rc rows status
    out="$(printf '%s\nSELECT id, s FROM t ORDER BY id;\n' "$prologue" \
           | "$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null)"
    rc=$?
    rows="$(printf '%s' "$out" | grep -c 'guarded')"
    status=nonzero
    [ "$rc" -eq 0 ] && status=zero
    echo "$label: guarded rows on stdout=$rows exit=$status"
}
attempt "wrong key" "PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$other';"
attempt "no key at all" ""
attempt "correct key" "PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';"
