-- PROFILE.md section 1 requires every `UPDATE OR ...` conflict form. IGNORE
-- and REPLACE are in update-conflict-forms.sql; the three that make the
-- statement fail are here, each observed through what survives, per
-- lang_conflict.html.
CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER UNIQUE);
INSERT INTO t VALUES(1,10),(2,20),(3,30);

-- ABORT: "backs out any changes made by the current SQL statement; but changes
-- caused by prior SQL statements within the same transaction are preserved and
-- the transaction remains active", so the COMMIT still lands the first UPDATE.
BEGIN;
UPDATE t SET b = 11 WHERE a = 1;
UPDATE OR ABORT t SET b = 30 WHERE a = 2;
COMMIT;
SELECT a, b FROM t ORDER BY a;

-- FAIL: "does not back out prior changes of the SQL statement that failed nor
-- does it end the transaction" - the transaction is still active, so the
-- COMMIT lands the first UPDATE here too.
BEGIN;
UPDATE t SET b = 12 WHERE a = 1;
UPDATE OR FAIL t SET b = 30 WHERE a = 2;
COMMIT;
SELECT a, b FROM t ORDER BY a;

-- ROLLBACK: "rolls back the current transaction", so the first UPDATE of this
-- transaction is lost and the row keeps its committed value.
BEGIN;
UPDATE t SET b = 13 WHERE a = 1;
UPDATE OR ROLLBACK t SET b = 30 WHERE a = 2;
SELECT a, b FROM t ORDER BY a;
