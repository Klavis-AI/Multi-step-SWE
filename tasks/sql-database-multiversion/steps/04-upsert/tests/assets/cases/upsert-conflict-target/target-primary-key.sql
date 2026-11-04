-- PROFILE.md 4.2: a conflict target may name the column of an INTEGER PRIMARY
-- KEY or of any other PRIMARY KEY.  lang_UPSERT.html's own examples use a TEXT
-- PRIMARY KEY.
CREATE TABLE i(k INTEGER PRIMARY KEY, c TEXT);
INSERT INTO i VALUES(1,'one'),(2,'two');
INSERT INTO i VALUES(2,'TWO') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM i ORDER BY k;
CREATE TABLE s(k TEXT PRIMARY KEY, c TEXT);
INSERT INTO s VALUES('a','one'),('b','two');
INSERT INTO s VALUES('b','TWO') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM s ORDER BY k;
-- a composite PRIMARY KEY declared as a table constraint
CREATE TABLE p(x INTEGER, y INTEGER, c TEXT, PRIMARY KEY(x,y));
INSERT INTO p VALUES(1,1,'a'),(1,2,'b');
INSERT INTO p VALUES(1,2,'B') ON CONFLICT(x,y) DO UPDATE SET c=excluded.c;
SELECT x,y,c FROM p ORDER BY x,y;
INSERT INTO p VALUES(2,2,'c') ON CONFLICT(x,y) DO UPDATE SET c=excluded.c;
SELECT x,y,c FROM p ORDER BY x,y;
PRAGMA integrity_check;
