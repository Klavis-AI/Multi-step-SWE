# R15.12: the key pragma's value domain. Non-hex text is refused, and so is
# hex that decodes to a length no recognised cipher uses; a key that decodes
# to one of the sizes a recognised cipher uses is taken. A refused key does
# not become the session's key, which the query form shows.
# The key pragma takes hex, and rejects both a non-hex string and a hex string
# of a length no supported cipher uses, naming the decoded length it got. A
# rejected key does not become the session's key.
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
PRAGMA hexkey = 'zz00';
PRAGMA hexkey = '0011';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f10';
PRAGMA hexkey;
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f';
PRAGMA hexkey;
SQL
