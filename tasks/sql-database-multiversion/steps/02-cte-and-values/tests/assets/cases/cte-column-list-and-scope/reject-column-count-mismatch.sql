CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p');
WITH c(x, y) AS (SELECT a, b FROM t) SELECT x, y FROM c;
-- PROFILE.md section 2.1: the column-name list must have exactly as many
-- names as the CTE's query returns columns. Too many ...
WITH c(x, y) AS (SELECT a FROM t) SELECT x FROM c;
-- ... and too few.
WITH c(x) AS (SELECT a, b FROM t) SELECT x FROM c;
SELECT count(*) FROM t;
