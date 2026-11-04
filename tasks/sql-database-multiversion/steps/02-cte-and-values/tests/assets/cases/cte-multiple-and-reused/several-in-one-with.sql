-- lang_with.html: "A single WITH clause can specify one or more common table
-- expressions."
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
WITH x(n) AS (SELECT 1), y(n) AS (SELECT 2) SELECT x.n, y.n FROM x, y;
WITH lo(k) AS (SELECT a FROM t WHERE a < 3), hi(k) AS (SELECT a FROM t WHERE a >= 3)
  SELECT (SELECT count(*) FROM lo), (SELECT count(*) FROM hi);
-- a later common table expression may reference an earlier one
WITH base(k) AS (SELECT a FROM t), dbl(m) AS (SELECT k * 2 FROM base)
  SELECT m FROM dbl ORDER BY m;
WITH one(k) AS (SELECT 1), two(k) AS (SELECT k + 1 FROM one), three(k) AS (SELECT k + 1 FROM two)
  SELECT k FROM three;
