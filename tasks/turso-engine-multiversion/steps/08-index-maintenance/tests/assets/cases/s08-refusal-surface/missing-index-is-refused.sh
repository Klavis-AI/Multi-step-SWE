# R08.1's negative half: `DROP INDEX` of a name that is not there is a refusal,
# and `IF EXISTS` makes the same statement a no-op.
#
# The step's documents state both halves of this and nothing in the corpus
# exercised either before this case: `spec/sqlite/lang_dropindex.html` gives
# `IF EXISTS` in the syntax, which exists only because dropping an index that
# is not there is otherwise an error, and `spec/PROFILE.md` rule 6 makes the
# refusal a report on standard output that does not stop the batch. Without
# this case an engine that answered every query correctly *and* silently
# accepted `DROP INDEX no_such_index` would pass every group at this step.
#
# The wording of the report is not graded. The reduction below keeps only this
# case's own labelled rows and collapses every other run of lines to the single
# token `ERROR`, so what is asserted is that a refusal landed at exactly that
# point in the stream, that the statements after it still ran, and that
# `IF EXISTS` on the same missing name produced no report at all.
#
# Stock sqlite3 is the second opinion on the refusal itself: a `.sh` case is
# outside the generator's automatic comparison, so the case makes it itself.
# Only accept-or-refuse and the resulting rows are compared, never the wording
# -- stock writes its diagnostic to standard error and carries on, so its rows
# are the same three labels the engine printed.
set -u
cat > case.sql <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x');
CREATE INDEX i_a ON t(a);
DROP INDEX no_such_index;
SELECT 'rows after refusal', count(*) FROM t;
DROP INDEX IF EXISTS no_such_index;
SELECT 'rows after if exists', count(*) FROM t;
DROP INDEX i_a;
SELECT 'index rows', count(*) FROM sqlite_schema WHERE type = 'index' AND name = 'i_a';
SQL
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null < case.sql
awk '{ if ($0 ~ /^[[:space:]]*$/) next
       if ($0 ~ /^(rows after refusal|rows after if exists|index rows)\|/) { print; e = 0; next }
       if (!e) print "ERROR"
       e = 1 }' raw.txt > graded.txt
cat graded.txt
grep -v '^ERROR$' graded.txt > engine-rows.txt
"$SQLITE3" -batch -list -separator '|' -noheader stock.db > stock-rows.txt 2>/dev/null < case.sql || true
sed -i '/^[[:space:]]*$/d' stock-rows.txt
if cmp -s engine-rows.txt stock-rows.txt
then echo "stock sqlite3 returns the same rows: yes"
else echo "stock sqlite3 returns the same rows: no"
fi
