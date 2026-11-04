-- lang_altertable.html: "The ALTER TABLE command works by modifing the SQL text
-- of the schema stored in the sqlite_master table.  No changes are made to
-- table content."  PROFILE.md 5.9: every row, and every value in the renamed
-- column, reads back exactly as it did before -- storage class included (step 1 section 2).  quote() renders each value
-- as an SQL literal so the bytes never reach standard output raw, except for
-- the BLOB row: lang_corefunc.html says of quote() only that "BLOBs are encoded
-- as hexadecimal literals" and lang_expr.html allows a BLOB literal either
-- spelling ("preceded by a single "x" or "X" character"), so the blob is read
-- with hex() instead, which the same page fixes as "the upper-case hexadecimal
-- rendering of the content of that blob".
CREATE TABLE t(k INTEGER PRIMARY KEY, old, n INTEGER);
INSERT INTO t(k,old,n) VALUES(1,'text',10),
                             (2,42,20),
                             (3,2.5,30),
                             (4,NULL,40),
                             (5,x'0102',50);
SELECT k, quote(old), typeof(old) FROM t WHERE k <> 5 ORDER BY k;
SELECT k, hex(old), typeof(old) FROM t WHERE k = 5;
SELECT count(*), sum(n) FROM t;
ALTER TABLE t RENAME COLUMN old TO new;
-- the same five rows, the same five values, the same five storage classes
SELECT k, quote(new), typeof(new) FROM t WHERE k <> 5 ORDER BY k;
SELECT k, hex(new), typeof(new) FROM t WHERE k = 5;
SELECT count(*), sum(n) FROM t;
-- a blob still compares equal to the same blob
SELECT k FROM t WHERE new = x'0102';
-- and the rowid mapping is untouched
SELECT rowid, k FROM t ORDER BY rowid;
PRAGMA integrity_check;
