-- A common table expression whose body is a VALUES clause is how a VALUES row
-- set acquires column names (PROFILE.md section 2.2).
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
WITH v(k, w) AS (VALUES(1,'one'),(2,'two'),(3,'three'))
  SELECT k, w FROM v WHERE k >= 2 ORDER BY k;
WITH v(k, w) AS (VALUES(1,'one'),(3,'three'))
  SELECT t.a, t.b, v.w FROM t JOIN v ON t.a = v.k ORDER BY t.a;
WITH v(i, s, n, r, b) AS (VALUES(1,'a',NULL,2.5,X'41'))
  SELECT typeof(i), typeof(s), typeof(n), typeof(r), typeof(b), hex(b) FROM v;
WITH v(k) AS (VALUES(1),(2),(3)) SELECT count(k), sum(k), max(k) FROM v;
