-- PROFILE.md 7: a rolled-back transaction leaves no trace.  That has to hold
-- for the rows an expression index holds as well as for the table rows, so a
-- query written against the indexed expression must answer as it did before
-- the transaction started.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER);
INSERT INTO t VALUES(1,-5),(2,3),(3,-1);
CREATE INDEX ta ON t(abs(a));
SELECT id FROM t WHERE abs(a)=5;
BEGIN;
UPDATE t SET a = 11 WHERE id = 1;
INSERT INTO t VALUES(4,-11);
DELETE FROM t WHERE id = 3;
SELECT id FROM t WHERE abs(a)=11 ORDER BY id;
SELECT id FROM t WHERE abs(a)=1;
ROLLBACK;
SELECT id FROM t WHERE abs(a)=11 ORDER BY id;
SELECT id FROM t WHERE abs(a)=5;
SELECT id FROM t WHERE abs(a)=1;
SELECT id, a FROM t ORDER BY id;
PRAGMA integrity_check;
-- A savepoint rolled back to has the same obligation (PROFILE.md 1, 7).
SAVEPOINT sp;
UPDATE t SET a = 100 WHERE id = 2;
SELECT id FROM t WHERE abs(a)=100;
ROLLBACK TO sp;
RELEASE sp;
SELECT id FROM t WHERE abs(a)=100;
SELECT id FROM t WHERE abs(a)=3;
-- An index created on an expression that a rejected statement would have
-- changed leaves the table as it was (PROFILE.md 7).
CREATE UNIQUE INDEX ua ON t(abs(a));
SELECT count(*) FROM t;
