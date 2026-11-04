# R08.2: after DROP INDEX, a query that could have used the index returns the
# identical result set, now via a table scan.
#
# What the lines observe. The same three-column query is run twice in one
# batch, once with the index present and once after it is dropped, and both
# runs print every selected row rather than a count -- so an engine that lost
# rows, reordered them, or answered the second run from a dropped structure
# shows it. `before:` and `after:` must be the same set of lines in the same
# order, `index-rows:` and `no-index-rows:` bracket them with the schema's own
# view of whether the index exists, and `sqlite-after:` is stock sqlite3
# re-reading the file the engine left behind and producing the same rows a
# third time.
#
# Before v0.0.20 DROP INDEX is a parse error, so the index is still there for
# the second run and the `no-index-rows:` line reads 1.
#
# This case does not assert that the dropped index's pages reach the freelist.
# Measured at v0.0.20 and at every later checkpoint through v0.1.5, they do
# not: the header freelist count stays 0 and stock sqlite3 reports the page as
# `never used`. The requirement list says what is graded here and the freelist
# is not part of it.
set -u

# Indexes are opt-in from v0.0.22 to v0.1.3 and the switch changes shape
# across that span, so ask the shell what it accepts rather than assuming. An
# engine with no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'p',10),(2,'q',20),(3,'r',30),(4,'s',40),(5,'u',50);
CREATE INDEX i_c ON t(c);
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'index-rows:'||count(*) FROM sqlite_schema WHERE type='index' AND name='i_c';
SELECT 'before:'||a||'/'||b||'/'||c FROM t WHERE c>=20 AND c<=40 ORDER BY c;
DROP INDEX i_c;
SELECT 'no-index-rows:'||count(*) FROM sqlite_schema WHERE type='index' AND name='i_c';
SELECT 'after:'||a||'/'||b||'/'||c FROM t WHERE c>=20 AND c<=40 ORDER BY c;
SELECT 'barrier:'||count(*) FROM t;
SQL

"$SQLITE3" -batch -list -noheader -separator '|' main.db 2>&1 <<'SQL' | sed 's/^/sqlite-/'
SELECT 'after:'||a||'/'||b||'/'||c FROM t WHERE c>=20 AND c<=40 ORDER BY c;
SELECT 'integrity-index-count:'||count(*) FROM sqlite_schema WHERE type='index' AND name='i_c';
SQL
