-- PROFILE.md 5.5: group_concat() used as a window function concatenates the
-- frame's values in the frame's own order -- the reading under which
-- windowfunctions.html's four worked group_concat results are true, and the
-- one the page explains in prose for the row with (a=3).  Every window here
-- totally orders its partition, which is the only case PROFILE.md 5.5 scores.
CREATE TABLE t(k INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'A'),(2,'B'),(3,'C'),(4,'D');
-- the default separator, and an explicit one
SELECT k, group_concat(s) OVER (ORDER BY k),
          group_concat(s,'.') OVER (ORDER BY k)
  FROM t ORDER BY k;
-- a sliding frame
SELECT k, group_concat(s,'.') OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND CURRENT ROW),
          group_concat(s,'.') OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- a descending window ORDER BY reverses the frame's order
SELECT k, group_concat(s,'.') OVER (ORDER BY k DESC) FROM t ORDER BY k;
