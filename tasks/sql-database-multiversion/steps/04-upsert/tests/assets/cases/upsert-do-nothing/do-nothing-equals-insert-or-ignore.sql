-- lang_UPSERT.html states the target-less form as an equivalence: "A DO
-- NOTHING upsert without a conflict target works the same as an INSERT OR
-- IGNORE."  The same multi-row insert is written both ways over two tables
-- that start out identical, and the two results are compared row by row
-- (PROFILE.md 4.3).
CREATE TABLE a1(k INTEGER PRIMARY KEY, u TEXT UNIQUE, v TEXT);
CREATE TABLE a2(k INTEGER PRIMARY KEY, u TEXT UNIQUE, v TEXT);
INSERT INTO a1 VALUES(1,'x','one'),(2,'y','two');
INSERT INTO a2 VALUES(1,'x','one'),(2,'y','two');
INSERT OR IGNORE INTO a1(k,u,v) VALUES(1,'p','P'),(3,'x','X'),(4,'w','W');
INSERT INTO a2(k,u,v) VALUES(1,'p','P'),(3,'x','X'),(4,'w','W')
  ON CONFLICT DO NOTHING;
SELECT k,u,v FROM a1 ORDER BY k;
SELECT k,u,v FROM a2 ORDER BY k;
-- neither table holds a row the other does not
SELECT count(*) FROM (SELECT k,u,v FROM a1 EXCEPT SELECT k,u,v FROM a2);
SELECT count(*) FROM (SELECT k,u,v FROM a2 EXCEPT SELECT k,u,v FROM a1);
PRAGMA integrity_check;
