# R03.11: BEGIN IMMEDIATE TRANSACTION and BEGIN EXCLUSIVE TRANSACTION mean
# what the same statements mean without the optional keyword -- both are
# accepted where nothing is open, which is the position the requirement names.
# R03.5: a BEGIN EXCLUSIVE TRANSACTION issued while a transaction is already
# open is refused.
# R03.12: a COMMIT, and an END, issued with no transaction open are refused.
# The optional TRANSACTION keyword. BEGIN IMMEDIATE TRANSACTION and BEGIN
# EXCLUSIVE TRANSACTION mean what BEGIN IMMEDIATE and BEGIN EXCLUSIVE mean, so
# they are accepted from the same checkpoint onwards; and a COMMIT with no
# transaction open is refused, in both its spellings, however the engine cares
# to word the refusal.
#
# BEGIN EXCLUSIVE TRANSACTION is graded twice, once refused while a transaction
# is open and once accepted with nothing open. The refused position alone would
# not distinguish an engine that correctly refuses a spelling it understands
# from one that does not parse the spelling at all: both emit output and both
# read as "refused". The accepted position is what separates them.
#
# Each statement is isolated between two markers and reduced to "accepted" or
# "refused", so no diagnostic wording is graded.
#
# Second opinion. Every verdict this case grades is asked of stock sqlite3
# 3.50.2 as well and printed beside the engine's, so neither the engine nor
# this expected output is the only witness. Stock reports a refusal on standard
# error rather than standard output, so its verdict is taken from its exit
# status under -bail -- which stops at the first statement it refuses -- rather
# than by folding its output: merging its two streams into one file would let
# stdout's block buffering reorder them against unbuffered stderr, which is not
# reproducible.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt 2>/dev/null <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
SELECT 'mark';
COMMIT;
SELECT 'mark';
END;
SELECT 'mark';
BEGIN IMMEDIATE TRANSACTION;
SELECT 'mark';
INSERT INTO t VALUES(2);
SELECT 'mark';
BEGIN EXCLUSIVE TRANSACTION;
SELECT 'mark';
COMMIT;
SELECT 'mark';
BEGIN EXCLUSIVE TRANSACTION;
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
for i in 1 2 3 5 7; do
    awk -v want="$i" '/^mark$/ { n++; next } n == want' raw.txt \
        | sed '/^[[:space:]]*$/d' > "b$i.txt"
done
verdict b1.txt "commit with nothing open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
COMMIT;
SQL
verdict b2.txt "end with nothing open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
END;
SQL
verdict b3.txt "begin immediate transaction" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
BEGIN IMMEDIATE TRANSACTION;
SQL
verdict b5.txt "begin exclusive transaction while one is open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
BEGIN IMMEDIATE TRANSACTION;
BEGIN EXCLUSIVE TRANSACTION;
SQL
verdict b7.txt "begin exclusive transaction with nothing open" <<'SQL'
CREATE TABLE t(a INTEGER PRIMARY KEY);
INSERT INTO t VALUES(1);
BEGIN EXCLUSIVE TRANSACTION;
SQL
