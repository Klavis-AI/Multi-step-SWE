# R15.1: declaring a cipher without supplying a key leaves the bytes the
# engine writes exactly as they were before encryption existed. The transform
# is configured and not engaged, so the file is not merely still-a-SQLite-file
# -- it is the same file, byte for byte, that the same workload produces with
# no cipher named at all.
#
# Three runs in three directories: one with no cipher, two with the cipher
# declared. Nothing about the digest's value is asserted, only that all three
# agree, so the case is insensitive to everything about the file except that
# the arrival of the page transform did not perturb it. The byte-identity of
# two independent runs is R15.1's own second clause, not a PROFILE.md rule;
# what PROFILE.md contributes is rule 3, whose last-in-first-out free-page
# reuse is why the DELETE and the later INSERT in the workload below land on
# the same page in every run rather than merely leaving the same rows behind.
#
# The file the cipher runs write is an ordinary unencrypted database, so it is
# also given the cross-read R15.2 asks for: stock sqlite3 opens cipher-a's
# file, answers PRAGMA integrity_check and returns the same rows this engine
# returns from it. What sqlite3 says when it is unhappy is not compared -- the
# answer is either the literal `ok` or something else.
set -u
cat > workload.sql <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'alpha'),(2,'bravo'),(3,'charlie');
DELETE FROM t WHERE id = 2;
INSERT INTO t VALUES(4,'delta');
PRAGMA wal_checkpoint(TRUNCATE);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
{ echo "PRAGMA cipher = 'aegis256';"; cat workload.sql; } > cipher-workload.sql
run() { mkdir -p "$1" && ( cd "$1" && "$CLI" -m "$CLI_MODE" -q main.db < "../$2" ) > /dev/null 2>&1; }
run nocipher   workload.sql
run cipher-a   cipher-workload.sql
run cipher-b   cipher-workload.sql
d() { sha256sum < "$1/main.db" | cut -d' ' -f1; }
if [ "$(d nocipher)" = "$(d cipher-a)" ]; then
    echo "declaring a cipher changed the file: no"
else
    echo "declaring a cipher changed the file: yes"
fi
if [ "$(d cipher-a)" = "$(d cipher-b)" ]; then
    echo "two independent runs wrote identical bytes: yes"
else
    echo "two independent runs wrote identical bytes: no"
fi
echo "reserved_space $(od -An -tx1 -j 20 -N 1 cipher-a/main.db | tr -d ' \n')"
echo "rows read back with the cipher declared:"
( cd cipher-a && "$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
PRAGMA cipher = 'aegis256';
SELECT id, s FROM t ORDER BY id;
SQL
)
answer="$("$SQLITE3" -batch cipher-a/main.db 'PRAGMA integrity_check;' 2>&1)"
case "$answer" in
    ok) echo "stock sqlite3 integrity_check: ok" ;;
    *)  echo "stock sqlite3 integrity_check: not ok" ;;
esac
echo "rows stock sqlite3 reads from the same file:"
"$SQLITE3" -batch -list -separator '|' -noheader cipher-a/main.db \
    'SELECT id,s FROM t ORDER BY id;' 2>&1
