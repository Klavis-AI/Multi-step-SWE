-- A VALUES clause is a SELECT, so it may be the parenthesised subquery that
-- lang_select.html allows in a FROM clause.
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
SELECT * FROM (VALUES(1,'a'),(2,'b')) ORDER BY 1;
SELECT count(*) FROM (VALUES(1),(2),(3));
SELECT * FROM (VALUES(3),(1),(2)) ORDER BY 1 DESC;
WITH v(k, w) AS (VALUES(1,'one'),(3,'three'))
  SELECT t.a, t.b, v.w FROM t JOIN v ON t.a = v.k ORDER BY t.a;
SELECT count(*) FROM (VALUES(1),(2)), (VALUES(3),(4));
