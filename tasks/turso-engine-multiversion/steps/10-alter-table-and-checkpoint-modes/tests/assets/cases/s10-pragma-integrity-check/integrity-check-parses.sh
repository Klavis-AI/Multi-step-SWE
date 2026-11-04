# R10.9: PRAGMA integrity_check parses and executes, returning ok for a clean
# database, and stock sqlite3 agrees about the same file.
#
# PRAGMA integrity_check parses and executes at this checkpoint, returning ok
# for a clean database, where earlier checkpoints rejected it as "Not a valid
# pragma name". That matters beyond this step: it is the structural check
# every cross-reading case in the step ends with, and it is unavailable
# before v0.0.22.
#
# .sh rather than .sql: this checkpoint frames every result block with a blank
# line, which a .sql case's graded rendering strips.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x'),(2,'y');
PRAGMA integrity_check;
SELECT count(*) FROM t;
SQL
echo "stock_sqlite3: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
