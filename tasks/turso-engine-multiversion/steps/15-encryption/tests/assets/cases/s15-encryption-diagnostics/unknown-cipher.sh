# R15.11: a cipher name the engine does not implement is refused, leaves no
# cipher selected -- the query form returns no row at all, not an empty one
# -- and does not end the session, which then takes a recognised name.
# A cipher name the engine does not implement is refused by name, and the
# session carries on with no cipher selected. The diagnostic is graded
# refused, and that is what is asserted -- the name it is refused by name is
# not. A .sh case because the shell prints the diagnostic on stdout followed
# by a blank line, which a .sql case is not allowed to emit.
#
# The wording of a diagnostic is not graded. Every line carrying the
# shell's box-drawing error marker is folded to the single word ERROR, so
# what is asserted is that the refusal lands at exactly that point in the
# stream, that it produced no rows of its own, and that what comes after it
# is unharmed. Blank separator lines come and go along the release chain and
# are removed; every row printed below has two or more columns, so nothing
# removed could have been a result row.
#
# Stock sqlite3 is not a second reader for this case. Neither `PRAGMA cipher`
# nor `PRAGMA hexkey` exists in SQLite, and SQLite silently ignores a pragma
# name it does not recognise -- measured against the pinned 3.50.2 fixture,
# which runs both of them with no diagnostic, no row and exit 0. So there is
# no second opinion to be had on whether a value is refused, on what the query
# form answers after a refusal, or on the session surviving it. Where stock
# sqlite3 does have an opinion at this step is the unencrypted file, and the
# two cases that write one cross-read it
# (s15-encryption-off-format/default-path-is-byte-reproducible and
# plaintext-file-still-sqlite).
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA cipher = 'rot13';
PRAGMA cipher;
PRAGMA cipher = 'aegis256';
PRAGMA cipher;
SQL
