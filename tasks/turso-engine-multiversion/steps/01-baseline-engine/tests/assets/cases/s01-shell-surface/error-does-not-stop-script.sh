# R01.13: a refused statement is reported and the script keeps going.
#
# R01.13 says a statement that cannot be executed is refused rather than
# silently ignored: reported on standard output, yielding no rows of its own,
# with the next statement still running. This case exercises four different
# refusals -- unknown function, duplicate table, unknown column, unknown table
# -- and puts a labelled count between every pair, so what is asserted is
# where each refusal lands in the stream, that it produced nothing, and that
# the table is unharmed on the far side of it. The wording is folded away, as
# R01.13's last sentence requires.
#
# No stock sqlite3 cross-read, and there is nothing here for one to check.
# What R01.13 fixes is this engine's shell surface: stock sqlite3 writes its
# diagnostics to standard *error*, not standard output, with different wording
# and no `×` marker, and `-batch` stops at the first error rather than carrying
# on. Every one of those differences is deliberate on this side --
# `spec/PROFILE.md` rules 6 and 7 -- so a comparison against stock sqlite3
# would assert a disagreement rather than catch one. The SQL results either
# side of each refusal are ordinary and are cross-read by the `.sql` cases in
# `s01-dml-and-select`.
# A statement that cannot be executed is refused rather than silently accepted:
# an error is reported at exactly that point in the stream, the statement
# itself yields no rows, and the statements after it still run against a table
# that is unharmed.
#
# The wording of a diagnostic is not graded. Every line carrying the shell's
# error marker is folded to the single word ERROR, so what is asserted is where
# the refusals happen, how many there are, and what the table holds around
# them. Blank separator lines come and go along the release chain and are
# removed; every row selected below has two columns, so nothing removed could
# have been a result row.
#
# The engine's own status is captured, so the output is written to a file and
# folded afterwards rather than piped. `$?` after a pipeline is the *last*
# command's status -- sed's, not the engine's -- and R01.6's `exit 0` half is
# exactly what the last line below asserts.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
SELECT 'start', count(*) FROM t;
SELECT nosuchfunc(1);
SELECT 'after unknown function', count(*) FROM t;
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
SELECT 'after duplicate table', count(*) FROM t;
SELECT nosuchcolumn FROM t;
SELECT 'after unknown column', count(*) FROM t;
SELECT * FROM nosuchtable;
SELECT 'after unknown table', count(*) FROM t;
INSERT INTO t VALUES(2,'two');
SELECT a, b FROM t ORDER BY a;
SELECT 'final', count(*) FROM t;
SQL
engine_rc=$?
sed -e '/^[[:space:]]*│/d' -e '/^[[:space:]]*$/d' -e 's/^.*×.*$/ERROR/' raw.txt
echo "engine_exit: $engine_rc"
