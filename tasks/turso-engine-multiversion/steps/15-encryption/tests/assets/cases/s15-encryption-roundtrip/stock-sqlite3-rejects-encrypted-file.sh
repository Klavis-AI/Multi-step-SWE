# R15.8: the byte-level interoperability guarantee every earlier step relies on
# is explicitly suspended while encryption is on. Stock sqlite3 opening an
# encrypted database must NOT report an intact one -- the pages it would have
# to walk are ciphertext. The engine holding the key reads the same file
# without complaint, which is what makes this a statement about encryption and
# not about a corrupt writer.
#
# What sqlite3 says when it refuses is not compared: its answer is folded to
# `ok` or `not ok`, and only the fold is asserted.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'sealed one'),(2,'sealed two');
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
echo "engine with the key:"
"$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
SELECT id, s FROM t ORDER BY id;
SQL
answer="$("$SQLITE3" -batch enc.db 'PRAGMA integrity_check;' 2>&1)"
case "$answer" in
    ok) echo "stock sqlite3 integrity_check: ok" ;;
    *)  echo "stock sqlite3 integrity_check: not ok" ;;
esac
rows="$("$SQLITE3" -batch -list -separator '|' -noheader enc.db \
        'SELECT id,s FROM t ORDER BY id;' 2>/dev/null | grep -c 'sealed')"
echo "stock sqlite3 rows recovered: $rows"
