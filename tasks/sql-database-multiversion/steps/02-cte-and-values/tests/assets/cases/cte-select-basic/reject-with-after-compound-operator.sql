CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
WITH c(x) AS (SELECT a FROM t) SELECT x FROM c ORDER BY x;
-- lang_with.html: "The WITH clause cannot be prepended to the second or
-- subsequent SELECT statement of a compound select."
SELECT 9 UNION ALL WITH d(y) AS (SELECT 8) SELECT y FROM d;
SELECT count(*) FROM t;
