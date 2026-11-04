-- "this decision is made separately for each row of the insert": a second row
-- of the same statement carrying the same key sees what the first row did,
-- because by then the first row is in the table (PROFILE.md 4.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER);
INSERT INTO t(k,c,n) VALUES(5,'p',1),(5,'q',2)
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
SELECT count(*) FROM t;
-- three rows with the same key: one insert followed by two updates
INSERT INTO t(k,c,n) VALUES(7,'a',1),(7,'b',10),(7,'c',100)
  ON CONFLICT(k) DO UPDATE SET c=c||excluded.c, n=n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- the same, with DO NOTHING: the first row is inserted and the rest skipped
INSERT INTO t(k,c,n) VALUES(9,'x',1),(9,'y',2),(9,'z',3) ON CONFLICT DO NOTHING;
SELECT k,c,n FROM t ORDER BY k;
PRAGMA integrity_check;
