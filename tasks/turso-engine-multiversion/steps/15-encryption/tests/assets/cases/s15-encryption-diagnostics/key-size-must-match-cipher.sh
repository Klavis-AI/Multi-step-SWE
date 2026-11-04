# R15.12: the key and the cipher are checked against each other when the second
# of the two is set. A 256-bit cipher refuses a 128-bit key -- a key that is
# perfectly well-formed hex, and that a 128-bit cipher would accept -- so no
# encryption context is ever built from a mismatched pair. The refusal is not
# terminal: the same session then takes a key of the size the cipher wants and
# encrypts with it.
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
#
# The database this case leaves behind is encrypted, which stock sqlite3
# cannot read either -- a graded requirement in its own right (R15.8),
# asserted by s15-encryption-roundtrip/stock-sqlite3-rejects-encrypted-file.
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f';
PRAGMA cipher;
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'encrypted after the refusal');
-- The writing invocation ends by reading back the table it wrote, and the row
-- it returns is graded -- it is what shows the write went through the context
-- that survived the refusal above. Durability itself does not depend on the
-- read-back: spec/PROFILE.md's durability rule requires a process that writes
-- and then exits to leave its committed data recoverable by the next reader
-- that opens the files.
SELECT id, v FROM t;
SQL
echo "reserved_space $(od -An -tx1 -j 20 -N 1 main.db | tr -d ' \n')"
