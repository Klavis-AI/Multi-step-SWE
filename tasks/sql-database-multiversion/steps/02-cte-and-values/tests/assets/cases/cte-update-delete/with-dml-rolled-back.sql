-- A WITH clause on INSERT, UPDATE and DELETE inside a transaction that is
-- rolled back leaves no trace (PROFILE.md step 1 section 7).
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q');
BEGIN;
WITH s(k, v) AS (VALUES(3,'r')) INSERT INTO t(a, b) SELECT k, v FROM s;
WITH s(k) AS (SELECT a FROM t WHERE a = 1) UPDATE t SET b = 'Z' WHERE a IN (SELECT k FROM s);
WITH s(k) AS (SELECT a FROM t WHERE a = 2) DELETE FROM t WHERE a IN (SELECT k FROM s);
SELECT a, b FROM t ORDER BY a;
ROLLBACK;
SELECT a, b FROM t ORDER BY a;
