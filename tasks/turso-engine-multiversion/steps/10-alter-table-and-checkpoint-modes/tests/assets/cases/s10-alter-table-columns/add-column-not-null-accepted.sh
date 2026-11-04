# R10.4: ADD COLUMN ... NOT NULL without a DEFAULT is accepted, the rows
# already on disk read NULL in the new column, a later INSERT that supplies a
# value lands, and stock sqlite3 re-parses the file and reads the same rows.
#
# The previous checkpoint aborts the process on this statement. What is
# graded here is acceptance and the NULL read-back of pre-existing rows --
# both stable to the end of the chain. Whether a NEW row may leave the
# column NULL is deliberately not asserted: this checkpoint permits it and
# later checkpoints in the chain enforce the constraint, so the case neither
# inserts a NULL into the new column nor reads pragma_table_info's `notnull`
# flag, either of which would pin a value that changes.
#
# .sh rather than .sql because stock sqlite3 refuses the statement outright
# ("Cannot add a NOT NULL column with default value NULL"), so there is no
# stock counterpart for the statement itself; the file it leaves behind is
# ordinary SQLite and stock sqlite3 is the second reader of that.
#
# Stock's `PRAGMA integrity_check` is deliberately NOT run here, and must not
# be added back. It is not a stable observation of anything this step grades:
# it answers about the rebuilt `sqlite_schema.sql` text, which this checkpoint
# does not grade (`TEST_INTERFACE.md`, "The schema text ALTER TABLE
# rebuilds": no case at this step reads the `sql` text back after an
# `ADD COLUMN`, and nothing here requires that text to be right or wrong).
# A `NOT NULL` marker that survives the rebuild makes stock report
# `NULL value in t.b` once per pre-existing row -- the rows R10.4 requires to
# read NULL -- while a rebuild that drops the marker answers `ok`. Both are
# conforming; the marker's survival is free here, so an `integrity: ok`
# expectation would fail a correct engine for the free choice, and an
# expectation of the error lines would pin a diagnostic. The file's
# readability by stock is carried by the two cross-reads below, which read
# the added column's shape and rows out of the file this engine wrote.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
ALTER TABLE t ADD COLUMN b TEXT NOT NULL;
SELECT a, typeof(b) FROM t ORDER BY a;
INSERT INTO t VALUES(3,'three');
SELECT a, b FROM t ORDER BY a;
SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;
SELECT count(*) FROM t;
SQL
# Stock sqlite3's stderr is merged on purpose: a stock-side failure must land
# in the graded output rather than vanish.
echo "stock_columns:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;" 2>&1
echo "stock_rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT a, b FROM t ORDER BY a;' 2>&1
