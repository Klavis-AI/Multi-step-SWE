-- A CTE feeding an INSERT that violates a UNIQUE index: lang_conflict.html's
-- default ABORT algorithm rejects the statement and leaves the table as it was.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT UNIQUE);
INSERT INTO t VALUES(1,'p');
WITH s(k, v) AS (VALUES(2,'q')) INSERT INTO t(a, b) SELECT k, v FROM s;
SELECT a, b FROM t ORDER BY a;
WITH s(k, v) AS (VALUES(3,'p')) INSERT INTO t(a, b) SELECT k, v FROM s;
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
