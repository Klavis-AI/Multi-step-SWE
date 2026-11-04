# R15.3: the same write workload, run once with no encryption and once through
# an engaged aegis256 key, produces two files that agree on page 1's magic and
# disagree past the page-1 header. The plaintext file carries the row text
# where a reader can find it; the encrypted file does not carry it anywhere,
# and its page 2 -- the table's first leaf -- is not the plaintext page 2.
#
# Both workloads end in PRAGMA wal_checkpoint(TRUNCATE), and that line is load
# bearing rather than tidiness. spec/PROFILE.md's durability rule leaves it
# open whether a writer that has exited left its committed rows in the log or
# folded them into the database file -- both are conforming -- and says a
# checkpoint is the only way to *require* the fold. The carve-out it makes for
# fields a reader sees by reading page 1 alone does not reach page 2 or the
# row bytes. So without the checkpoint a conforming engine that defers the
# fold would be compared on a database file holding no rows at all: the two
# absence assertions would pass for the wrong reason and the two positive ones
# would fail. The checkpoint puts both files into the one state the documents
# let a case require, and it costs an engine that folds eagerly nothing.
#
# No ciphertext byte is compared against a fixed value. There is a fresh
# per-page nonce, so the encrypted file is different on every run; every
# assertion below is about what is absent, or about two files differing, and
# both survive that.
#
# Stock sqlite3 is the second reader for the control arm only. It opens the
# unencrypted file, reports it intact and returns the same two rows, which is
# what makes "the row text is in the plaintext file" a two-engine claim rather
# than this engine's own word about its own output. On the encrypted arm there
# is no second opinion to be had: stock sqlite3 cannot read an encrypted
# database at all, and that inability is itself a graded requirement here
# (R15.8), asserted by
# s15-encryption-roundtrip/stock-sqlite3-rejects-encrypted-file.
set -u
cat > workload.sql <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'distinctive-row-text-alpha'),(2,'distinctive-row-text-bravo');
-- Fold the committed rows into the database file. Everything this case reads
-- lies past the page-1 header, where spec/PROFILE.md's durability rule leaves
-- a writer free to keep the rows in the log instead, and names this statement
-- as the only way to require otherwise.
PRAGMA wal_checkpoint(TRUNCATE);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
{
  echo "PRAGMA cipher = 'aegis256';"
  echo "PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';"
  cat workload.sql
} > encrypted-workload.sql
mkdir -p plain enc
( cd plain && "$CLI" -m "$CLI_MODE" -q main.db < ../workload.sql ) > /dev/null 2>&1
( cd enc && "$CLI" -m "$CLI_MODE" -q main.db < ../encrypted-workload.sql ) > /dev/null 2>&1
present() { if grep -aq 'distinctive-row-text' "$1"; then echo yes; else echo no; fi; }
echo "row text in the unencrypted file: $(present plain/main.db)"
echo "row text in the encrypted file:   $(present enc/main.db)"
magic() { od -An -tx1 -j 0 -N 16 "$1" | tr -s ' \n' ' '; }
if [ "$(magic plain/main.db)" = "$(magic enc/main.db)" ]; then
    echo "page-1 magic identical: yes"
else
    echo "page-1 magic identical: no"
fi
page2() { dd if="$1" bs=4096 skip=1 count=1 2>/dev/null | sha256sum | cut -d' ' -f1; }
if [ "$(page2 plain/main.db)" = "$(page2 enc/main.db)" ]; then
    echo "page 2 identical: yes"
else
    echo "page 2 identical: no"
fi
echo "stock sqlite3 on the unencrypted control file:"
answer="$("$SQLITE3" -batch plain/main.db 'PRAGMA integrity_check;' 2>&1)"
case "$answer" in
    ok) echo "  integrity_check: ok" ;;
    *)  echo "  integrity_check: not ok" ;;
esac
"$SQLITE3" -batch -list -separator '|' -noheader plain/main.db \
    'SELECT id,s FROM t ORDER BY id;' 2>&1 | sed 's/^/  /'
