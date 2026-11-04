# R11.5: generate_series is callable at this checkpoint at all -- the previous
# one refuses it as "No such table-valued function" -- in a FROM clause, under
# aggregates, and as the source of an INSERT ... SELECT.
#
# No stock cross-read is added here: the identical constructs go through the
# differential guard already, in the two .sql siblings in this group, which
# gen-matrix.sh rejects unless stock sqlite3 returns the same rows.
# The generate_series table-valued function arrives at this checkpoint: the
# previous checkpoint refuses it as "No such table-valued function". It is
# usable in a FROM clause and in an INSERT ... SELECT, and its rows agree with
# stock sqlite3 for the same bounds.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT value FROM generate_series(1,4);
SELECT value FROM generate_series(0,10,5);
SELECT count(*) FROM generate_series(1,100);
SELECT sum(value) FROM generate_series(1,10);
CREATE TABLE t(a INTEGER);
INSERT INTO t SELECT value FROM generate_series(1,3);
SELECT count(*) FROM t;
SQL
