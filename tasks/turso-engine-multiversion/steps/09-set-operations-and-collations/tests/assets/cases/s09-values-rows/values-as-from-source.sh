# R09.2: VALUES used as the source of a SELECT, against a real table holding the same rows.
# A VALUES list used as a FROM-clause source yields the same rows, in the
# order the list writes them, as a real table holding the same data.
#
# Before v0.0.21 the FROM-clause VALUES form is unimplemented. Stock sqlite3
# creates the comparison table.
#
# Line 5 of the script below is the one refusal this step grades: `a` is not a
# column of a VALUES list -- its columns are named `column1`, `column2`, ... --
# so the statement cannot resolve and must be reported, without stopping the
# batch and without producing a row. The fold turns whatever report is printed
# into the single token ERROR; its wording is never compared.
#
# The differential is at the bottom. A .sh case is not covered by the
# generator's automatic sqlite3 comparison, so the case makes it itself. It
# cannot be a whole-script `cmp` here, because stock sqlite3 writes its
# diagnostic for the refused statement to standard error and prints nothing on
# standard output for it, so the two streams have different line counts by
# construction. Instead stock is asked the same statements the case grades --
# the VALUES projection and its count, the mirror table, and whether the
# unresolvable-column statement is refused at all -- and its answers are
# graded beside the engine's.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE mirror(a INTEGER, b TEXT);
INSERT INTO mirror VALUES(1,'x'),(2,'y'),(3,'z');
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT * FROM (VALUES(1,'x'),(2,'y'),(3,'z'));
SELECT 'm:'||a||b FROM mirror ORDER BY a;
SELECT 'vc:'||count(*) FROM (VALUES(1,'x'),(2,'y'),(3,'z'));
SELECT 'mc:'||count(*) FROM mirror;
SELECT 'sum:'||sum(a) FROM (VALUES(1),(2),(3));
SELECT 'pick:'||b FROM mirror WHERE a=2;
SQL

echo "sqlite-values:$("$SQLITE3" -batch -list -separator '|' -noheader :memory: "SELECT * FROM (VALUES(1,'x'),(2,'y'),(3,'z'));" | tr '\n' '/')"
echo "sqlite-vc:$("$SQLITE3" -batch -list -noheader :memory: "SELECT count(*) FROM (VALUES(1,'x'),(2,'y'),(3,'z'));")"
echo "sqlite-mirror:$("$SQLITE3" -batch -list -noheader main.db "SELECT a||b FROM mirror ORDER BY a;" | tr '\n' '/')"
if "$SQLITE3" -batch -list -noheader :memory: "SELECT 'sum:'||sum(a) FROM (VALUES(1),(2),(3));" > /dev/null 2>&1; then
  echo 'sqlite-refuses-unresolvable:no'
else
  echo 'sqlite-refuses-unresolvable:yes'
fi
