-- lang_with.html: "An ordinary common table expression works as if it were a
-- view that exists for the duration of a single statement."
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'p',10),(2,'q',20),(3,'r',30);
WITH c(x) AS (SELECT 1) SELECT x FROM c;
WITH s AS (SELECT a, b FROM t WHERE a >= 2) SELECT a, b FROM s ORDER BY a;
WITH s(k, v) AS (SELECT a, c FROM t) SELECT k, v FROM s WHERE v > 10 ORDER BY k;
WITH s(k) AS (SELECT a FROM t) SELECT count(k), min(k), max(k) FROM s;
