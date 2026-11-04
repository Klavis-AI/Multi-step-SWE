# R10.7: a mode argument that is not one of SQLite's checkpoint modes is
# refused rather than silently treated as the default mode, and the session
# carries on with its rows intact.
#
# A mode argument that is not one of SQLite's checkpoint modes is reported as
# an error rather than being treated as the default mode. The wording is not
# graded -- only that the refusal lands at exactly that point in the stream,
# that it produced no rows, and that the connection stayed usable.
#
# This is the one place in the step where the engine and stock sqlite3
# deliberately disagree, and the last line states the disagreement instead of
# hiding it. Stock sqlite3 3.50.2 does NOT refuse an unrecognised mode name:
# it treats `PRAGMA wal_checkpoint(NOSUCHMODE)` as PASSIVE and answers a
# triple. Refusing it is the stricter reading and it is what R10.7 requires,
# so the case grades the engine's refusal and records stock's acceptance
# beside it. Only accept-or-refuse is compared, never the wording:
# spec/TEST_INTERFACE.md's "error report" fold has already collapsed the
# engine's refusal to ERROR.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(7),(8);
PRAGMA wal_checkpoint(NOSUCHMODE);
SELECT a FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
if "$SQLITE3" -batch stock.db > /dev/null 2>&1 <<'SQL'
PRAGMA journal_mode=WAL;
CREATE TABLE t(a INTEGER);
PRAGMA wal_checkpoint(NOSUCHMODE);
SQL
then echo "stock_sqlite3: accepted"
else echo "stock_sqlite3: refused"
fi
