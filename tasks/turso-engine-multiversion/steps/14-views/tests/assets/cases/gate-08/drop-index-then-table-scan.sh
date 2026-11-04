# R08.1: DROP INDEX removes the index's row from sqlite_schema.
# R08.2: the query that could have used the index still answers, from the
# table scan that replaces it.
#
# What the lines observe. `before-drop:` and `after-drop:` are the engine's own
# reads of sqlite_schema either side of the DROP, so the schema row's
# disappearance is the observation rather than an inference from the DROP not
# erroring. `query-still-works:` re-runs the indexed predicate afterwards and
# `barrier:` counts the whole table, showing the batch ran to the end and the
# rows survived the drop.
#
# R08.1 is graded on the schema row alone. Whether the dropped index's pages
# reach the freelist is NOT asserted here or anywhere at this step: measured at
# v0.0.20 and at every later checkpoint through v0.1.5, they do not reach it --
# the page-1 freelist count stays 0 and stock sqlite3 reports the orphaned page
# as `never used` -- so an assertion about the freelist would pin a defect.
#
# Before v0.0.20 the statement is entirely unimplemented. Stock sqlite3 creates
# table and index; the engine drops the index.
#
# This is milestone 8's gate case: it is staged as `gate-08` at every later
# step. Its statements and its expected output are frozen. Only this comment
# has been rewritten.
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
INSERT INTO t VALUES(1,'p',10),(2,'q',20),(3,'r',30),(4,'s',40);
CREATE INDEX i_c ON t(c);
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'before-drop:'||count(*) FROM sqlite_schema WHERE type='index' AND name='i_c';
DROP INDEX i_c;
SELECT 'after-drop:'||count(*) FROM sqlite_schema WHERE type='index' AND name='i_c';
SELECT 'query-still-works:'||count(*) FROM t WHERE c>=20;
SELECT 'barrier:'||count(*) FROM t;
SQL
