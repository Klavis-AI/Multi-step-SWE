# R15.1 and R15.2: encryption is opt-in at the level of the *pair*. A session
# that names a cipher but never supplies a key has the transform configured
# and not engaged, so the file it writes is an ordinary SQLite database --
# no reserved space per page, and stock sqlite3 reads every row and reports
# the file intact.
# Encryption is opt-in, and every earlier format guarantee has to survive its
# arrival unchanged. A session that names a cipher but never supplies a key
# writes an ordinary SQLite file: the page-1 header still declares a 4096-byte
# page with no reserved space per page -- reserved space is what an engaged
# page transform would claim for its nonce and tag -- and stock sqlite3 reads
# the rows and reports an intact database.
#
# The session block below prints the two pragma query forms, whose row texts
# TEST_INTERFACE.md fixes: the cipher's canonical name, and the sentence the
# key pragma answers with while no key is held. Blank separator lines come and
# go along the release chain and are removed, as in every other case here;
# every row printed by that block is non-empty, so nothing removed could have
# been a result row.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
PRAGMA cipher = 'aegis256';
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'plaintext one'),(2,'plaintext two');
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
hex() { od -An -tx1 -j "$1" -N "$2" main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//'; }
echo "session:"
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
PRAGMA cipher;
PRAGMA hexkey;
SQL
echo "magic          $(hex 0 16)"
echo "page_size      $(hex 16 2)"
echo "reserved_space $(hex 20 1)"
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,s FROM t ORDER BY id;' 2>&1
