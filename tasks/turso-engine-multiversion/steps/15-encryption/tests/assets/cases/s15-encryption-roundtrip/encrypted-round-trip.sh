# R15.6, and R15.3 from the outside: a database written through an ENGAGED
# cipher-and-key pair is read back, by a second process that supplies the same
# pair, as exactly the rows that were written -- and those rows are not sitting
# in the file in the clear.
#
# This is the on-path case for the milestone. The off-path control is
# s15-encryption-off-format/plaintext-file-still-sqlite, which declares a
# cipher and never supplies a key and therefore writes an ordinary SQLite
# file; that case establishes that the feature is opt-in, not that it works.
# Nothing here compares ciphertext bytes: there is a fresh per-page nonce, so
# encrypting the same rows twice does not produce the same file. What is
# compared is what comes back out of it, and the absence of what must not be
# in it.
#
# Stock sqlite3 is not a second reader for anything this case asserts: it
# cannot read an encrypted database at all, so there is no second opinion to
# be had on what an engaged cipher wrote or on what comes back through it.
# That inability is a graded requirement in its own right (R15.8) and is
# asserted by the sibling case stock-sqlite3-rejects-encrypted-file.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'ciphertext marker alpha'),(2,'ciphertext marker bravo');
-- Fold the committed rows into the database file. The absence assertion
-- below reads the database file past the page-1 header, where
-- spec/PROFILE.md's durability rule leaves a writer free to keep the rows in
-- the log instead and names this statement as the only way to require
-- otherwise. Without it the grep would be searching a file that holds no rows
-- at all, and would report the row text absent for the wrong reason.
PRAGMA wal_checkpoint(TRUNCATE);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
echo "reopened with the key:"
"$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
SELECT id, s FROM t ORDER BY id;
SELECT count(*) FROM t;
SQL
if grep -aq 'ciphertext marker' enc.db; then
    echo "row text readable in the database file: yes"
else
    echo "row text readable in the database file: no"
fi
