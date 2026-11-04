# R05.16: a blob that is not valid JSONB, and a JSON path that does not parse,
# are refused where they stand -- the call returns no row, the batch carries
# on, and the stored document is still readable afterwards.
#
# The document under test is a JSONB value in a column, not a literal, and it
# is re-read after every refused call: `stored type` and `stored doc` at the
# end show the blob was neither consumed nor rewritten by a call that failed.
# Two kinds of refusal are exercised -- a path with no `$`, a path that is a
# bare trailing dot, and two blobs whose first byte is not a header any reader
# can follow -- and one thing that is deliberately NOT a refusal: a path that
# parses but selects nothing answers NULL, which the last row records as a
# distinct outcome.
#
# Every row the engine must return carries a leading tag, and the awk fold
# below turns only a run of UNtagged output into the single token ERROR. So
# the wording of a refusal is never compared, a refusal that gains or loses
# the `×` marker at a later checkpoint folds the same way, and an engine that
# wrongly ACCEPTS a malformed path prints `no dollar|...` where the expected
# output has ERROR and fails there rather than folding into the same ERROR as
# a correct refusal.
#
# This is a .sh case because that fold is not something a .sql case can apply,
# and a .sh case is not replayed against stock sqlite3 by the harness. The
# second opinion is therefore written in at the bottom: stock 3.50.2 runs the
# same batch against its own file and its standard output is exactly the rows
# that must come back, so a row missing here and a row returned that stock
# refused each break the comparison. Its exit status is non-zero because of
# those refusals and says nothing, so it is discarded.
set -u
cat > case.sql <<'SQL'
CREATE TABLE d(id INTEGER PRIMARY KEY, doc BLOB);
INSERT INTO d VALUES(1, jsonb('{"a":1,"n":{"b":2}}'));
SELECT 'stored type', typeof(doc) FROM d WHERE id = 1;
SELECT 'good extract', json_extract(doc, '$.a') FROM d WHERE id = 1;
SELECT 'no dollar', json_extract(doc, 'a') FROM d WHERE id = 1;
SELECT 'still here', json_extract(doc, '$.n.b') FROM d WHERE id = 1;
SELECT 'trailing dot', json_extract(doc, '$.') FROM d WHERE id = 1;
SELECT 'still here', json_extract(doc, '$.n.b') FROM d WHERE id = 1;
SELECT 'not jsonb', json(x'ff');
SELECT 'still here', json_extract(doc, '$.n.b') FROM d WHERE id = 1;
SELECT 'not jsonb type', json_type(x'c0ffee');
SELECT 'still here', json_extract(doc, '$.n.b') FROM d WHERE id = 1;
SELECT 'stored type', typeof(doc) FROM d WHERE id = 1;
SELECT 'stored doc', json(doc) FROM d WHERE id = 1;
SELECT 'absent path is null', json_extract(doc, '$.zz') IS NULL FROM d WHERE id = 1;
SQL
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null < case.sql
awk '{ if ($0 ~ /^[[:space:]]*$/) next
       if ($0 ~ /^(stored type|stored doc|good extract|still here|no dollar|trailing dot|not jsonb|not jsonb type|absent path is null)\|/) { print; e = 0; next }
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
