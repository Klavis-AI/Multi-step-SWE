-- The inherited INSERT OR ... conflict forms still apply when the source of
-- the INSERT is a common table expression.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT UNIQUE);
INSERT INTO t VALUES(1,'p');
WITH s(k, v) AS (VALUES(2,'p'),(3,'q')) INSERT OR IGNORE INTO t(a, b) SELECT k, v FROM s;
SELECT a, b FROM t ORDER BY a;
SELECT changes();
WITH s(k, v) AS (VALUES(1,'zz')) INSERT OR REPLACE INTO t(a, b) SELECT k, v FROM s;
SELECT a, b FROM t ORDER BY a;
