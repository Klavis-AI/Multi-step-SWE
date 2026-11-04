# R03.11: BEGIN IMMEDIATE and BEGIN EXCLUSIVE are accepted where the previous
# checkpoint refused them.
# R03.5: a BEGIN, and a second BEGIN IMMEDIATE, issued while one is already
# open are refused, and refusing them does not disturb the open transaction.
# R03.4: a statement that cannot run is refused inside the open transaction
# rather than swallowed by it, and a query issued afterwards still answers.
# The transaction-control statements that arrive with this checkpoint, each one
# graded on whether the engine took it or refused it. BEGIN IMMEDIATE and BEGIN
# EXCLUSIVE are accepted where they were refused before; a BEGIN issued while
# one of them is open is refused, and so is a second BEGIN IMMEDIATE; and a
# statement that cannot be executed is still refused inside an open transaction
# rather than being swallowed by it.
#
# Each statement is isolated between two markers and reduced to "accepted" or
# "refused", so no diagnostic wording is graded and neither is the number of
# lines a diagnostic occupies.
#
# Second opinion. Every verdict this case grades, and the one ordinary SQL
# result it grades, is asked of stock sqlite3 3.50.2 as well and printed beside
# the engine's answer, so neither the engine nor this expected output is the
# only witness. Stock reports a refusal on standard error rather than standard
# output, so its verdict is taken from its exit status under -bail -- which
# stops at the first statement it refuses -- rather than by folding its output:
# merging its two streams into one file would let stdout's block buffering
# reorder them against unbuffered stderr, which is not reproducible.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
SELECT 'mark';
BEGIN IMMEDIATE;
SELECT 'mark';
BEGIN;
SELECT 'mark';
BEGIN IMMEDIATE;
SELECT 'mark';
SELECT nosuchfunc(1);
SELECT 'mark';
SELECT count(*) FROM t;
SELECT 'mark';
COMMIT;
SELECT 'mark';
BEGIN EXCLUSIVE;
SELECT 'mark';
COMMIT;
SELECT count(*) FROM t;
SQL
stock_verdict () {   # the SQL to try arrives on stdin
    rm -f sq.db sq.db-journal sq.db-wal
    if "$SQLITE3" -batch -bail sq.db > /dev/null 2>&1
    then echo accepted
    else echo refused
    fi
}
verdict () {   # <block file> <label>; stock's copy of the same statement on stdin
    local stock engine
    stock="$(stock_verdict)"
    if [ -s "$1" ]; then engine=refused; else engine=accepted; fi
    printf '%s: %s (stock sqlite3: %s)\n' "$2" "$engine" "$stock"
}
for i in 1 2 3 4 5 7; do
    awk -v want="$i" '/^mark$/ { n++; next } n == want' raw.txt \
        | sed '/^[[:space:]]*$/d' > "b$i.txt"
done
verdict b1.txt "begin immediate" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN IMMEDIATE;
SQL
verdict b2.txt "begin while one is open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN IMMEDIATE;
BEGIN;
SQL
verdict b3.txt "begin immediate while one is open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN IMMEDIATE;
BEGIN IMMEDIATE;
SQL
verdict b4.txt "a statement that cannot run, inside the transaction" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN IMMEDIATE;
SELECT nosuchfunc(1);
SQL
verdict b7.txt "begin exclusive" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN EXCLUSIVE;
SQL
echo "a query inside the transaction still answers:"
cat b5.txt
echo "stock sqlite3 answers the same query:"
rm -f sq.db sq.db-journal sq.db-wal
"$SQLITE3" -batch -list -separator '|' -noheader sq.db <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'one');
BEGIN IMMEDIATE;
SELECT count(*) FROM t;
COMMIT;
SQL
