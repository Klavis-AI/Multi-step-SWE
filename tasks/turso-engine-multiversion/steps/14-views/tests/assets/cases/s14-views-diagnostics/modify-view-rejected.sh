# R14.6: INSERT, UPDATE and DELETE aimed at a view are each refused and the base table is left exactly as it was.
# A view has no storage, so every DML form aimed at one is refused and the
# base table is left exactly as it was. This is a .sh case because the shell
# prints its diagnostics on stdout together with a trailing blank line that a
# .sql case is not allowed to emit.
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
# process. The closing SELECTs are the writing batch's own read-back of that
# state, and they are kept because they are the in-process half of the
# comparison the stock read below completes -- the same rows, asserted once by
# the engine that wrote them and once by a foreign reader. Do not remove them:
# dropping them would leave the stock read with nothing to be compared against.
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
CREATE TABLE base(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO base VALUES(1,'one'),(2,'two');
CREATE VIEW v AS SELECT id, v FROM base;
INSERT INTO v VALUES(3,'three');
UPDATE v SET v='changed' WHERE id=1;
DELETE FROM v WHERE id=2;
SELECT id, v FROM base ORDER BY id;
SELECT count(*) FROM base;
SQL

# Stock sqlite3 as a second opinion, and its limit. The refusal itself has a
# stock counterpart and it agrees: stock sqlite3 refuses an INSERT, an UPDATE
# and a DELETE aimed at a view. What has no stock counterpart is the shape
# graded above -- stock writes its diagnostic to standard error and stops the
# batch at the first error, so there is no in-stream position for a refusal to
# land at and no continuation to observe. The base table the refusals were
# supposed to leave alone is comparable, and is compared here.
echo "sqlite rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, v FROM base ORDER BY id;' 2>&1
