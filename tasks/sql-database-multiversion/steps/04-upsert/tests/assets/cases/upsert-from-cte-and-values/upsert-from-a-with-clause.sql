-- Step 2 section 2.1 puts a WITH clause in front of an INSERT, and PROFILE.md
-- 4.1 says an upsert is an ordinary INSERT with a trailing clause, so the two
-- compose.  lang_insert.html requires the SELECT to carry a WHERE clause when
-- an upsert clause is present (PROFILE.md 4.1).
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER DEFAULT 0);
CREATE TABLE src(k INTEGER, c TEXT, n INTEGER);
INSERT INTO t VALUES(1,'one',10),(2,'two',20);
INSERT INTO src VALUES(1,'ONE',1),(3,'THREE',3);
WITH s(k,c,n) AS (SELECT k,c,n FROM src)
INSERT INTO t(k,c,n) SELECT k,c,n FROM s WHERE 1
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=t.n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- two chained CTEs, the second reading the first
WITH a(k) AS (SELECT k FROM src),
     b(k,c) AS (SELECT k, 'from-b-'||k FROM a WHERE k > 1)
INSERT INTO t(k,c) SELECT k,c FROM b WHERE 1
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c,n FROM t ORDER BY k;
-- a CTE whose body is an aggregate over another table
WITH agg(k,total) AS (SELECT k, sum(n) FROM src GROUP BY k)
INSERT INTO t(k,c,n) SELECT k,'agg',total FROM agg WHERE 1
  ON CONFLICT(k) DO UPDATE SET n=t.n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
PRAGMA integrity_check;
