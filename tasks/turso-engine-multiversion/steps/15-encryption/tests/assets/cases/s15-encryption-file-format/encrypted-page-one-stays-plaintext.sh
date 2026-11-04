# R15.5: encryption transforms page contents, not the file's identity. With a
# cipher and a key both engaged, page 1 still opens with the plaintext
# `SQLite format 3\0` magic and the standard header fields fileformat2.html
# lays out at offsets 16 to 99 are still readable where that document puts
# them -- the page size at 16, the write and read format versions at 18 and
# 19, and the three payload fractions at 21, 22 and 23, which the format fixes
# at 64, 32 and 32.
#
# The one field that moves is the reserved-space byte at offset 20: it is 0 on
# every unencrypted file this engine writes, and it is non-zero here because
# an engaged page transform claims the tail of every page for its per-page
# nonce and tag -- 48 bytes for aegis256. That byte is the file-format
# signature of an engaged cipher, and it is what
# s15-encryption-off-format/plaintext-file-still-sqlite asserts is 00 when the
# cipher is declared and no key is supplied.
#
# Stock sqlite3 is not a second reader for this case. The page-1 fields are
# read with od, which is independent of this engine and of SQLite alike; the
# rows can only come back through a reader that holds the key, and stock
# sqlite3 cannot read an encrypted database at all. That inability is a
# graded requirement in its own right (R15.8) and is asserted by
# s15-encryption-roundtrip/stock-sqlite3-rejects-encrypted-file. The
# unencrypted counterpart of these same header fields is cross-read against
# stock sqlite3, by s15-encryption-off-format/plaintext-file-still-sqlite.
set -u
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q enc.db > /dev/null 2>&1 <<SQL
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'header probe one'),(2,'header probe two');
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
hex() { od -An -tx1 -j "$1" -N "$2" enc.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'; }
echo "magic            $(hex 0 16)"
echo "page_size        $(hex 16 2)"
echo "format_versions  $(hex 18 2)"
echo "reserved_space   $(hex 20 1)"
echo "payload_fracs    $(hex 21 3)"
echo "rows through the encrypted pager:"
"$CLI" -m "$CLI_MODE" -q enc.db 2>/dev/null <<SQL | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA hexkey = '$key';
SELECT id, s FROM t ORDER BY id;
SQL
