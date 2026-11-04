-- lang_UPSERT.html: "The conflict target specifies a specific uniqueness
-- constraint that will trigger the upsert."  A target that identifies no
-- uniqueness constraint of the table is rejected and leaves the table exactly
-- as it was (PROFILE.md 4.2, 4.7).
CREATE TABLE t(k INTEGER PRIMARY KEY, b TEXT, c TEXT);
CREATE INDEX bi ON t(b);
INSERT INTO t VALUES(1,'x','one');
-- a column carrying a UNIQUE constraint is a legal target
INSERT INTO t VALUES(1,'y','ONE') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,b,c FROM t ORDER BY k;
-- a column carrying an ordinary, non-unique index is not
INSERT INTO t VALUES(2,'x','two') ON CONFLICT(b) DO UPDATE SET c=excluded.c;
SELECT k,b,c FROM t ORDER BY k;
-- nor is a column with no index at all
INSERT INTO t VALUES(3,'z','three') ON CONFLICT(c) DO UPDATE SET b=excluded.b;
SELECT k,b,c FROM t ORDER BY k;
-- nor is a subset of a multi-column unique constraint
CREATE TABLE m(x INTEGER, y INTEGER, z TEXT, UNIQUE(x,y));
INSERT INTO m VALUES(1,2,'p');
INSERT INTO m VALUES(1,3,'q') ON CONFLICT(x) DO UPDATE SET z=excluded.z;
SELECT x,y,z FROM m ORDER BY x,y;
SELECT count(*) FROM t;
SELECT count(*) FROM m;
PRAGMA integrity_check;
