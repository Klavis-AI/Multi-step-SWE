# R02.9 and R02.10: SQLite's `$.a[0]` path syntax -- an array subscript after
# an object key, nested to any depth, and `$[0]` at the root -- the refusal of
# a malformed path, and what a JSON function does with a blob argument that is
# not valid JSON.
#
# The refusals are counted, not read. Their wording changes along the release
# chain and is not part of the contract; what the contract fixes is that a
# malformed path is refused rather than silently answering NULL, that the
# refusal produces no row of its own, and that the statements after it still
# run -- which is why the last statement repeats the first working extraction.
# The rows are separated from the refusals by the label each SELECT carries in
# its first column.
#
# How the refusals are counted matters, and this case counts them twice over,
# by two measures that a conforming engine satisfies whatever its reports look
# like. `statements-refused` is derived from the rows: eleven SELECTs go in,
# each labelled, so a label that does not come back is a statement that
# produced no row. That is exactly what PROFILE.md rule 6 promises and it does
# not depend on the shape of a report at all. An earlier version of this case
# counted the non-row lines left on stdout instead, which quietly required
# every report to occupy exactly one line after the `|` gutter is stripped --
# a layout no document fixes, since rule 7 makes everything after the marker
# free text. An engine whose report ran to two unindented lines would have
# been failed for a report it was entitled to write.
#
# `each-refusal-reported` keeps the other half of rule 6, which the row-derived
# count alone would drop: a refusal has to be *reported on standard output*,
# not merely silent. It asserts at least one non-row line per refused
# statement, so an engine that swallows a refusal fails, while an engine whose
# reports are longer than the reference's still passes. Neither measure reads a
# single character of the report's wording.
#
# This is a .sh case, so the harness does not run the automatic stock-sqlite3
# differential; it is written out here instead. Every row the engine produces
# has to be the row stock sqlite3 produces for the same script.
set -u

cat > script.sql <<'SQL'
SELECT 'p-index', json_extract('{"a":[7,8]}','$.a[0]');
SELECT 'p-index-2', json_extract('{"a":[7,8]}','$.a[1]');
SELECT 'p-nested', json_extract('{"a":{"b":[1,{"c":9}]}}','$.a.b[1].c');
SELECT 'p-root-array', json_extract('[{"k":5}]','$[0].k');
SELECT 'p-missing', json_extract('{"a":1}','$.zz') IS NULL;
SELECT 'bad-no-root', json_extract('{"a":1}','a');
SELECT 'bad-dangling-dot', json_extract('{"a":1}','$.');
SELECT 'b-valid', json_valid(x'4142');
SELECT 'b-object', json_object('k', x'4142');
SELECT 'b-array', json_array(x'4142');
SELECT 'after', json_extract('{"a":[7,8]}','$.a[1]');
SQL

"$CLI" -m "$CLI_MODE" -q main.db < script.sql 2>/dev/null \
    | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d' > engine.txt
grep -E '^[a-z][a-z0-9-]*\|' engine.txt > engine-rows.txt || true

echo "rows:"
cat engine-rows.txt

statements=$(grep -cE '^SELECT ' script.sql)
returned=$(grep -c . engine-rows.txt || true)
refused=$(( statements - returned ))
reported=$(grep -cvE '^[a-z][a-z0-9-]*\|' engine.txt || true)

echo "statements-refused: $refused"
if [ "$reported" -ge "$refused" ]; then
    echo "each-refusal-reported: yes"
else
    echo "each-refusal-reported: no"
fi

"$SQLITE3" -batch -list -separator '|' -noheader ref.db < script.sql 2>/dev/null \
    | sed '/^[[:space:]]*$/d' \
    | grep -E '^[a-z][a-z0-9-]*\|' > stock-rows.txt || true
if cmp -s engine-rows.txt stock-rows.txt; then
    echo "sqlite3-agrees: yes"
else
    echo "sqlite3-agrees: no"
fi
