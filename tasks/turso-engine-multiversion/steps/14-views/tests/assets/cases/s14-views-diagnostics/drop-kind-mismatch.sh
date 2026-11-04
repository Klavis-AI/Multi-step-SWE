# R14.7: DROP TABLE naming a view and DROP VIEW naming a table are each refused, and the schema is left intact.
# DROP TABLE and DROP VIEW are not interchangeable: each refuses an object of
# the other kind, with its own diagnostic, and leaves the schema intact. The
# diagnostics are refused in each direction and that is asserted, but their
# wording is not.
#
# The wording of a diagnostic is not graded. Every line carrying the
# shell's box-drawing error marker is folded to the single word ERROR, so
# what is asserted is that the refusal lands at exactly that point in the
# stream, that it produced no rows of its own, and that what comes after it
# is unharmed. Blank separator lines come and go along the release chain and
# are removed; every row printed below has two or more columns, so nothing
# removed could have been a result row.
#
# Durability handoff. `spec/PROFILE.md`'s durability rule puts the obligation
# on the writing invocation: it must not return until its committed data is
# recoverable from the files on disk, so the stock reader at the end of this
# case sees a complete file without anything being done to the engine's own
# process. The closing `SELECT count(*) FROM base` is the writing batch's own
# read-back of that state, and it is kept because it is the in-process half of
# the comparison the stock read below completes -- the same count, asserted
# once by the engine that wrote it and once by a foreign reader. Do not remove
# it: dropping it would leave the stock read with nothing to be compared
# against.
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE base(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO base VALUES(1,'one');
CREATE VIEW vw AS SELECT id FROM base;
DROP TABLE vw;
DROP VIEW base;
SELECT type, name FROM sqlite_schema ORDER BY name;
SELECT count(*) FROM base;
SQL

# Stock sqlite3 as a second opinion, and its limit. The two refusals
# themselves do have a stock counterpart and it agrees: stock sqlite3 refuses
# `DROP TABLE` naming a view and `DROP VIEW` naming a table, in both
# directions. What has no stock counterpart is the shape graded above -- stock
# writes its diagnostic to standard error and stops the batch at the first
# error, so there is no in-stream position for a refusal to land at and no
# continuation to observe. The state the refusals left behind is comparable,
# and is compared here: a foreign reader has to see both objects still there.
echo "sqlite schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT type,name FROM sqlite_schema ORDER BY name;' 2>&1
echo "sqlite rows: $("$SQLITE3" -batch -list -noheader main.db 'SELECT count(*) FROM base;' 2>&1)"
