-- lang_with.html: a WITH clause may be prepended to an UPDATE statement.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'p',10),(2,'q',20),(3,'r',30);
WITH s(k) AS (SELECT a FROM t WHERE c >= 20) UPDATE t SET b = 'Z' WHERE a IN (SELECT k FROM s);
SELECT a, b, c FROM t ORDER BY a;
WITH s(k, n) AS (VALUES(1,100),(3,300))
  UPDATE t SET c = (SELECT n FROM s WHERE k = t.a) WHERE a IN (SELECT k FROM s);
SELECT a, b, c FROM t ORDER BY a;
SELECT changes();
