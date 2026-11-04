-- PROFILE.md 4.2: a conflict target may name the column of a UNIQUE column
-- constraint, of a UNIQUE table constraint, or of a CREATE UNIQUE INDEX.
-- All three are step 1 section 1 uniqueness constraints.
CREATE TABLE c1(k INTEGER PRIMARY KEY, u TEXT UNIQUE, v TEXT);
INSERT INTO c1 VALUES(1,'x','one');
INSERT INTO c1 VALUES(2,'x','TWO') ON CONFLICT(u) DO UPDATE SET v=excluded.v;
SELECT k,u,v FROM c1 ORDER BY k;
CREATE TABLE c2(k INTEGER PRIMARY KEY, u TEXT, v TEXT, UNIQUE(u));
INSERT INTO c2 VALUES(1,'x','one');
INSERT INTO c2 VALUES(2,'x','TWO') ON CONFLICT(u) DO UPDATE SET v=excluded.v;
SELECT k,u,v FROM c2 ORDER BY k;
CREATE TABLE c3(k INTEGER PRIMARY KEY, u TEXT, v TEXT);
CREATE UNIQUE INDEX c3u ON c3(u);
INSERT INTO c3 VALUES(1,'x','one');
INSERT INTO c3 VALUES(2,'x','TWO') ON CONFLICT(u) DO UPDATE SET v=excluded.v;
SELECT k,u,v FROM c3 ORDER BY k;
-- the same table can be upserted through either of its two constraints
INSERT INTO c1 VALUES(1,'y','ONE') ON CONFLICT(k) DO UPDATE SET u=excluded.u;
SELECT k,u,v FROM c1 ORDER BY k;
PRAGMA integrity_check;
