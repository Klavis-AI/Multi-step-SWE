# R15.9: the checkpoint path has to keep working underneath an ENGAGED page
# transform, not merely underneath a configured one. Every checkpoint mode
# still returns SQLite's result triple, the rows stay readable through the
# encrypted pager afterwards, and a second process that supplies the same
# cipher and key finds every row the checkpoint copied.
#
# The unengaged counterpart is s15-encryption-off-format/
# checkpoint-with-cipher-declared, which runs the same shape with a cipher
# declared and no key.
#
# Stock sqlite3 is not a second reader for this case, on either half of it.
# It cannot open the encrypted database being checkpointed, so it has no
# opinion on the rows that come back after the fold. It does not agree on the
# checkpoint triple even where it can open the file: run the SQL of the step's
# own s15-synchronous-pragma/checkpoint-under-synchronous-off through the
# pinned $SQLITE3 fixture and it answers 0|-1|-1 to each checkpoint, because it
# is not in WAL mode there and reports -1 for the counts it did not attempt,
# where this engine answers 0|0|0 with the real frame counts. Measured against
# the 3.50.2 fixture; the two agree only on the leading 0 that means the
# checkpoint completed. There is therefore no second opinion to be had here at
# all.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
echo "in the writing session:"
"$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b');
PRAGMA wal_checkpoint(TRUNCATE);
SELECT id, v FROM t ORDER BY id;
INSERT INTO t VALUES(3,'c');
PRAGMA wal_checkpoint(FULL);
SELECT id, v FROM t ORDER BY id;
SQL
echo "reopened with the key:"
"$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
SELECT id, v FROM t ORDER BY id;
SELECT count(*) FROM t;
SQL
