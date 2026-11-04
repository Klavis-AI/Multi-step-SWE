# R02.1, R02.3 and R02.4: what a checkpoint is for -- after it, the rows live
# in main.db itself. The
# pragma is issued, its own answer is reduced to whether the engine accepted it
# and returned a result triple, and then the -wal and -shm files are deleted
# before stock sqlite3 is allowed near the database. Anything sqlite3 can still
# see must therefore have been copied into the main file by the checkpoint
# rather than read out of the log.
#
# The triple's counts drift along the release chain, so they are not graded;
# the checkpoint's own invariants are -- nothing was busy, and the number of
# frames copied equals the number of frames in the log.
#
# Each of the three columns is matched against /^[0-9]+$/ before its value is
# used, which is what makes "busy is 0" an assertion about a rendered integer
# rather than about awk's numeric coercion. `$1 == 0` alone would also accept
# `0.0`, `+0`, `-0`, ` 0` and `0e0` -- measured under mawk, gawk and
# original-awk -- and R02.1 grades the triple as three *integer* columns in
# stock's shape, so a non-integral spelling of zero is a failure, not a pass.
# The sibling case checkpoint-is-invisible.sh matches all three the same way.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null <<'SQL'
CREATE TABLE p(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO p VALUES(1,'alpha');
INSERT INTO p VALUES(2,'bravo');
INSERT INTO p VALUES(3,'charlie');
CREATE TABLE q(k TEXT, n INTEGER);
INSERT INTO q VALUES('x',10);
SELECT 'MARK';
PRAGMA wal_checkpoint;
SELECT 'MARK';
-- The writing invocation ends by reading back every table it wrote.
-- PROFILE.md's durability rule already requires a writer to leave its
-- committed data recoverable from the files on disk, so a conforming engine
-- does not need this; it is here so that what this case grades is the
-- checkpoint, and not the commit path that step 01 already grades.
SELECT count(*) FROM p;
SELECT count(*) FROM q;
SQL
awk '/^MARK$/ { n++; next } n == 1' raw.txt \
    | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/' > during.txt
awk -F'|' 'END { printf "checkpoint-returned-a-triple: %s\n",
    (NR == 1 && NF == 3 && $1 ~ /^[0-9]+$/ && $1 == 0 \
     && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/ && $2 == $3) ? "yes" : "no" }' during.txt
rm -f main.db-wal main.db-shm
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>/dev/null)"
echo "schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT type,name FROM sqlite_schema ORDER BY name;" 2>/dev/null
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,s FROM p ORDER BY id; SELECT k,n FROM q ORDER BY k;' 2>/dev/null
