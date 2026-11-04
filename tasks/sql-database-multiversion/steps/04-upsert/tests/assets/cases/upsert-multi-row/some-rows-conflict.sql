-- lang_UPSERT.html: "In the case of a multi-row insert, this decision is made
-- separately for each row of the insert."  (PROFILE.md 4.3)
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER DEFAULT 0);
INSERT INTO t VALUES(1,'one',10),(3,'three',30);
-- rows 1 and 3 collide and are updated; rows 2 and 4 do not and are inserted
INSERT INTO t(k,c,n) VALUES(1,'ONE',11),(2,'TWO',22),(3,'THREE',33),(4,'FOUR',44)
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=t.n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- the same statement over a source in which nothing collides
INSERT INTO t(k,c,n) VALUES(5,'FIVE',5),(6,'SIX',6)
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=t.n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- and over one in which everything collides
INSERT INTO t(k,c,n) VALUES(5,'v',1),(6,'w',1)
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=t.n+excluded.n;
SELECT k,c,n FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
