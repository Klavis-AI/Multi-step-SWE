-- PROFILE.md 5.1: the SELECT that feeds an INSERT ... SELECT may carry window
-- functions, "including one carrying step 4's upsert clause".  The window
-- function is computed over the SELECT's result set, and the upsert then
-- applies row by row (step 4 sections 4.1, 4.4).  Each SELECT carries the
-- WHERE clause step 4 section 4.1 requires before an upsert-clause.
CREATE TABLE src(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO src VALUES(1,10),(2,20),(3,30);
CREATE TABLE dst(k INTEGER PRIMARY KEY, rn INTEGER, total INTEGER);
INSERT INTO dst VALUES(1,0,0);
INSERT INTO dst(k,rn,total)
  SELECT k, row_number() OVER (ORDER BY k), sum(v) OVER (ORDER BY k) FROM src WHERE 1
  ON CONFLICT(k) DO UPDATE SET rn = excluded.rn, total = excluded.total;
SELECT k, rn, total FROM dst ORDER BY k;
-- the same insert again, this time conflicting on every row
INSERT INTO dst(k,rn,total)
  SELECT k, row_number() OVER (ORDER BY k DESC), sum(v) OVER () FROM src WHERE 1
  ON CONFLICT(k) DO UPDATE SET rn = excluded.rn, total = dst.total + 1;
SELECT k, rn, total FROM dst ORDER BY k;
-- DO NOTHING leaves the existing rows alone
INSERT INTO dst(k,rn,total)
  SELECT k, 99, 99 FROM src WHERE 1 ON CONFLICT(k) DO NOTHING;
SELECT k, rn, total FROM dst ORDER BY k;
-- step 2's printf() over a window result
SELECT printf('%d:%d', k, row_number() OVER (ORDER BY k)) AS s
  FROM src ORDER BY k;
PRAGMA integrity_check;
