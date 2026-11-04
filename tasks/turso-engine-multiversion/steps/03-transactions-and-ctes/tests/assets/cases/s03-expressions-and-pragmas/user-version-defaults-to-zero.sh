# R03.18: a freshly created database reports PRAGMA user_version as 0, and the
# four bytes at page-1 header offset 60 that back it are zero in the file too,
# as read by od and by stock sqlite3.
# A freshly created database reports user_version 0, and the field that backs
# it at page-1 header offset 60 is zero in the file as well -- as read by od
# and by stock sqlite3, neither of which asks this engine anything. An engine
# that answers the pragma from memory without writing the header is caught by
# the disagreement.
#
# The pragma's own answer is isolated between two markers and reduced to the
# number it returned, or to the words "not answered" if it returned anything
# else, so no diagnostic wording is graded.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
SELECT 'mark';
PRAGMA user_version;
SELECT 'mark';
-- This invocation ends with a count over the table it wrote, the durability
-- barrier this package requires of every case that hands a file between
-- processes -- spec/PROFILE.md's durability rule. The count is read back
-- below as a graded row.
SELECT 'count', count(*) FROM t;
SQL
awk '/^mark$/ { n++; next } n == 1' raw.txt | sed '/^[[:space:]]*$/d' > pv.txt
awk 'END { printf "pragma user_version: %s\n",
    (NR == 1 && $0 ~ /^[0-9]+$/) ? $0 : "not answered" }' pv.txt
grep '^count|' raw.txt
echo "header offset 60: $(od -An -tu4 -j60 -N4 --endian=big main.db | tr -d ' ')"
echo "stock sqlite3 says: $("$SQLITE3" -batch main.db 'PRAGMA user_version;' 2>&1)"
