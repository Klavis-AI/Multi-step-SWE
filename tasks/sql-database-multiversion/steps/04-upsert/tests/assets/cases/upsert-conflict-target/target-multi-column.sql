-- PROFILE.md 4.2: a target may name several columns, and it identifies a
-- constraint rather than an ordering, so the columns may be written in either
-- order.
CREATE TABLE m(x INTEGER, y INTEGER, z TEXT, UNIQUE(x,y));
INSERT INTO m VALUES(1,2,'p'),(3,4,'q');
INSERT INTO m VALUES(1,2,'P') ON CONFLICT(x,y) DO UPDATE SET z=excluded.z;
SELECT x,y,z FROM m ORDER BY x,y;
INSERT INTO m VALUES(3,4,'Q') ON CONFLICT(y,x) DO UPDATE SET z=excluded.z;
SELECT x,y,z FROM m ORDER BY x,y;
-- only the whole key collides: (1,4) shares a column with each existing row
-- and is therefore a new row
INSERT INTO m VALUES(1,4,'r') ON CONFLICT(x,y) DO UPDATE SET z=excluded.z;
SELECT x,y,z FROM m ORDER BY x,y;
-- a three-column unique index, targeted in a shuffled order
CREATE TABLE n(a INTEGER, b INTEGER, c INTEGER, t TEXT);
CREATE UNIQUE INDEX nabc ON n(a,b,c);
INSERT INTO n VALUES(1,1,1,'first');
INSERT INTO n VALUES(1,1,1,'second') ON CONFLICT(c,a,b) DO UPDATE SET t=excluded.t;
SELECT a,b,c,t FROM n ORDER BY a,b,c;
PRAGMA integrity_check;
