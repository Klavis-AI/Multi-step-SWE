# R10.3: ADD COLUMN with a DEFAULT makes the short records already on disk
# read the default value without being rewritten, a row inserted afterwards
# carries its own value, and the default expression is recorded in the schema.
#
# ADD COLUMN with a DEFAULT must not rewrite the rows that are already on
# disk: those rows are short one field, and reading them has to synthesise the
# default. A row inserted afterwards carries its own value, and the default
# expression itself has to be recorded in the schema.
#
# .sh rather than .sql because this checkpoint prints a blank separator line
# before every result block; the case strips those itself so only result lines
# are observed. Being a .sh case, it is outside the generator's automatic
# stock-sqlite3 comparison, so it makes that comparison itself on the last
# lines: every value here -- including `dflt_value`'s quoted rendering -- is
# something stock sqlite3 produces for the same statements, and the case is
# only sound if the two agree.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(id INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1),(2);
ALTER TABLE t ADD COLUMN tag TEXT DEFAULT 'none';
INSERT INTO t VALUES(3,'set');
SELECT id, tag, typeof(tag) FROM t ORDER BY id;
SELECT count(*) FROM t WHERE tag = 'none';
SELECT cid, name, type, dflt_value FROM pragma_table_info('t') ORDER BY cid;
SELECT count(*) FROM t;
SQL
# Stock sqlite3 runs the same statements on its own file. Its stderr is merged
# on purpose: a stock-side failure must land in the graded output rather than
# vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1),(2);
ALTER TABLE t ADD COLUMN tag TEXT DEFAULT 'none';
INSERT INTO t VALUES(3,'set');
SELECT id, tag, typeof(tag) FROM t ORDER BY id;
SELECT count(*) FROM t WHERE tag = 'none';
SELECT cid, name, type, dflt_value FROM pragma_table_info('t') ORDER BY cid;
SELECT count(*) FROM t;
SQL
