# R01.14: a PRIMARY KEY violation -- a second row claiming a rowid already in
# use -- is refused under R01.13's contract, and the file left behind holds
# exactly the rows that were accepted.
#
# R01.14 names three things and this case observes all three: the conflicting
# row is not stored, the statements on either side of it still run, and stock
# sqlite3 reading the same file confirms which rows survived. The refusal
# itself is graded as R01.13's shape -- a report at that point in the stream,
# no rows of its own, the next statement still running -- which is why the
# requirement is worded as a specialisation of R01.13 rather than a separate
# error contract. `UNIQUE` constraints and `ON CONFLICT` clauses are both out
# of scope at this step and neither appears below: a UNIQUE constraint parses
# at v0.0.13 and is then not enforced -- both conflicting rows are stored and
# no sqlite_autoindex row is written -- so there is no correct expected output
# to freeze for it. The rowid conflict this case writes is the whole of what
# R01.14 grades.
#
# A constraint violation is a runtime diagnostic, not a fatal error: the row
# is rejected, the rows around it are kept, and the file that is left behind
# holds exactly the rows that were accepted.
#
# The wording of a diagnostic is not graded. Every line that reports an error
# -- the shell's box-drawing diagnostic marker or the engine's runtime-error
# line -- is folded to a single word, so what is asserted is that the refusal
# lands at exactly that point in the stream, that it produced no rows of its
# own, and that the rows on either side of it are all still there. This is the
# same fold the other diagnostic cases in this corpus use. Blank separator
# lines come and go along the release chain and are removed; every row printed
# below has two columns or is a bare count, so nothing removed could have been
# a result row.
#
# Standard error is never merged into this stream. The diagnostic is asserted
# on stdout, where the shell writes it, and a case that folded stderr in would
# pass a build that had moved the diagnostic to the wrong stream.
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/; s/^Runtime error:.*$/ERROR/'
CREATE TABLE u(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO u VALUES(1,'alpha');
INSERT INTO u VALUES(2,'beta');
INSERT INTO u VALUES(2,'beta-again');
INSERT INTO u VALUES(3,'gamma');
SELECT id, name FROM u ORDER BY id;
SELECT count(*) FROM u;
SQL
echo "rows-in-file:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,name FROM u ORDER BY id;' 2>&1
