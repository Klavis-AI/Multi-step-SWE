# R08.11: PRAGMA page_size returns the page size recorded in the database
# header, and returns the same value stock sqlite3 reads from the same file.
#
# What the lines observe. The first line is the engine's answer to the pragma
# over a file stock sqlite3 created; `from-table:` shows the same invocation
# also read the table, so the pragma did not come from a half-opened database;
# `sqlite-page-size:` is stock's reading of the same header field. The two
# numbers are the assertion -- the pragma reports the file's header, not a
# compiled-in constant.
#
# The file is deliberately created at a page size of 2048, which is neither
# stock sqlite3's default nor the 4096 that spec/PROFILE.md rule 1 fixes for a
# database this engine CREATES. That choice is the whole point of the case.
# An earlier version of it let stock create the file at its default, so both
# sides of the comparison read 4096 -- and an engine that answered the pragma
# from a compiled-in 4096 passed, which is exactly the claim the paragraph
# above makes and could not support. At 2048 the only way to produce the line
# is to read offset 16 of the page-1 header of the file that was opened, which
# is what R08.11 says.
#
# Why 2048 rather than a larger non-default size: measured 2026-09-21 across
# the checkpoint chain, a database at page size 8192 is not merely answered
# wrongly but aborts the process from v0.1.0 through v0.1.3 (`panicked at
# core/storage/sqlite3_ondisk.rs:930:55: range start index 8151 out of range
# for slice of length 4096`, exit 101), so a case built on it would regress at
# steps 11 to 13. 512 and 1024 also abort, at v0.1.2 and v0.1.3, with a
# different panic -- `read(1024) != expected(4096)` and `read(2048) !=
# expected(4096)`, exit 101 at v0.1.2 and 134 at v0.1.3. 2048 is the one
# non-default size that survives the whole chain: those checkpoints read two
# pages at a time, so 2048 is the only setting other than 4096 whose read
# length comes out at the 4096 they expect. It is answered correctly at
# v0.0.20 and at every later checkpoint through v0.1.5, six repeats each with
# no divergence.
#
# This capability lands at v0.0.20: the previous checkpoint does not answer the
# pragma at all.
set -u

"$SQLITE3" -batch main.db <<'SQL'
PRAGMA page_size=2048;
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA page_size;
SELECT 'from-table:'||count(*) FROM t;
SQL

echo "sqlite-page-size:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA page_size;')"
