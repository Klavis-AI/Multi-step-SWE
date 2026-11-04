# R03.16: a JSON path that does not parse is refused where it stands -- the
# call yields no row, the statements after it still run, the document is left
# unchanged -- while a path that parses but selects nothing answers NULL
# rather than erroring.
# A JSON path argument follows SQLite's $.a[0] syntax. A path that does not
# parse is refused where it stands -- the call returns nothing, the statement
# after it runs, and the document is left exactly as it was. A path that parses
# but selects nothing is not an error: it returns NULL, which is a different
# outcome and is recorded as such.
#
# The document the malformed calls are aimed at is a stored one, not a string
# literal, and it is read back after every refused call and after the accepted
# one. That is what makes "left exactly as it was" something this case can see:
# a json_set that damaged the value it was handed, or wrote its own result back
# over it, changes a later `stored doc` row. A literal cannot show either.
#
# Every row the engine is expected to return carries a leading tag, and the
# fold below recognises the tag of a row that must NOT come back -- `no dollar`,
# `trailing dot`, `double dot` -- as well as the tags of the rows that must. So
# an engine that wrongly accepts a malformed path prints `no dollar|...` where
# the expected output has ERROR, and fails, instead of folding into the same
# ERROR as a correct refusal. Only a run of output carrying no tag at all -- the
# refusal itself -- becomes ERROR, so no diagnostic wording is graded.
#
# All three malformed paths are ones stock sqlite3 3.50.2 also refuses, checked
# below. That matters twice over: the step's instruction tells the candidate to
# settle any answer they are unsure of against stock sqlite3, so a path this
# engine refuses and stock accepts would be a trap rather than a test.
set -u
cat > case.sql <<'SQL'
CREATE TABLE d(id INTEGER PRIMARY KEY, doc TEXT);
INSERT INTO d VALUES(1, '{"a":1,"n":{"b":2}}');
SELECT 'good', json_set('{"a":1}', '$.b', 2);
SELECT 'good nested', json_set('{"a":{"b":1}}', '$.a.b', 9);
SELECT 'good array', json_set('[1,2]', '$[0]', 9);
SELECT 'stored doc', doc FROM d WHERE id = 1;
SELECT 'no dollar', json_set(doc, 'a', 2) FROM d WHERE id = 1;
SELECT 'still here', json_set('{"a":1}', '$.b', 2);
SELECT 'stored doc', doc FROM d WHERE id = 1;
SELECT 'trailing dot', json_set(doc, '$.', 2) FROM d WHERE id = 1;
SELECT 'still here', json_set('{"a":1}', '$.b', 2);
SELECT 'stored doc', doc FROM d WHERE id = 1;
SELECT 'double dot', json_set(doc, '$..a', 2) FROM d WHERE id = 1;
SELECT 'still here', json_set('{"a":1}', '$.b', 2);
SELECT 'stored doc', doc FROM d WHERE id = 1;
SELECT 'accepted on the stored doc', json_set(doc, '$.n.b', 9) FROM d WHERE id = 1;
SELECT 'stored doc', doc FROM d WHERE id = 1;
SELECT 'absent path is null', json_extract('{"a":1}', '$.zz') IS NULL;
SELECT 'absent index is null', json_extract('[1,2]', '$[9]') IS NULL;
SQL
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null < case.sql
awk '{ if ($0 ~ /^[[:space:]]*$/) next
       if ($0 ~ /^(good|good nested|good array|stored doc|accepted on the stored doc|still here|absent path is null|absent index is null|no dollar|trailing dot|double dot)\|/) { print; e = 0; next }
       if (!e) print "ERROR"
       e = 1 }' raw.txt > graded.txt
cat graded.txt
# Second opinion. Stock sqlite3 answers the same batch. It reports a refusal on
# standard error and carries on, so its standard output is exactly the rows
# that must come back -- which makes it a direct check on the rows this engine
# returned, in both directions: a row missing here and an extra row the engine
# should have refused each break the comparison. Its exit status is non-zero
# because of those refusals and says nothing, so it is discarded.
grep -v '^ERROR$' graded.txt > engine-rows.txt
"$SQLITE3" -batch -list -separator '|' -noheader stock.db > stock-rows.txt 2>/dev/null < case.sql || true
sed -i '/^[[:space:]]*$/d' stock-rows.txt
if cmp -s engine-rows.txt stock-rows.txt
then echo "stock sqlite3 returns the same rows: yes"
else echo "stock sqlite3 returns the same rows: no"
fi
