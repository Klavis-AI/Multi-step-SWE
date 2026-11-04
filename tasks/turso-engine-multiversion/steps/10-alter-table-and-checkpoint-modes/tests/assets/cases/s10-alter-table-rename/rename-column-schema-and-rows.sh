# R10.2: RENAME COLUMN rewrites the column's name inside the stored CREATE
# TABLE text, and the new name selects the same values out of records written
# before the rename -- so the rename touched the schema and not the rows.
#
# ALTER TABLE ... RENAME COLUMN rewrites the column's name inside the stored
# CREATE TABLE text. The old name must stop resolving and the new one must
# select the same values out of records that were written before the rename,
# which is only true if the rename touched the schema and not the rows.
#
# The `sql` column is graded against spec/PROFILE.md rule 2's rendering, which
# is the engine's normalisation rather than stock sqlite3's verbatim text. The
# index half of R10.2, and the stock-sqlite3 re-parse the requirement names as
# its observable, are asserted by the sibling case
# rename-column-sqlite3-reparse.sh.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x'),(2,'y');
ALTER TABLE t RENAME COLUMN b TO label;
SELECT sql FROM sqlite_schema;
SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;
SELECT a, label FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 runs the same statements on its own file. A .sh case is outside
# the generator's automatic comparison, so the case makes it itself. The `sql`
# column is the one thing left out of that comparison, and it is left out for a
# stated reason: stock preserves the original text's spacing
# (`CREATE TABLE t(a INTEGER, label TEXT)`) where spec/PROFILE.md rule 2 fixes
# this engine's rendering, so the two cannot agree on that text by
# construction. Everything else the engine is graded on above -- the shape
# `pragma_table_info` reports under the new column name, the values the new
# name selects out of records written before the rename, and the count -- stock
# must produce identically. The re-parse of the rewritten `sql` and of the
# index that names the column is the sibling case
# rename-column-sqlite3-reparse.sh. Stock's stderr is merged on purpose: a
# stock-side failure must land in the graded output rather than vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x'),(2,'y');
ALTER TABLE t RENAME COLUMN b TO label;
SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;
SELECT a, label FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
