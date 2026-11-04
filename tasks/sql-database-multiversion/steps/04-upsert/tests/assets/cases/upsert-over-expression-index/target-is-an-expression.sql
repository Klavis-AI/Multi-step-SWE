-- PROFILE.md 4.2: a conflict target may name an expression, for a UNIQUE index
-- whose key is that expression (step 3 section 3.4).  It is written the way it
-- is written in the CREATE INDEX.
CREATE TABLE e(id INTEGER PRIMARY KEY, a INTEGER, note TEXT);
CREATE UNIQUE INDEX ea ON e(abs(a));
INSERT INTO e VALUES(1,-5,'neg5'),(2,3,'three');
-- +5 and -5 have the same indexed value, so this row collides
INSERT INTO e VALUES(3,5,'pos5') ON CONFLICT(abs(a)) DO UPDATE SET note=excluded.note;
SELECT id,a,note FROM e ORDER BY id;
SELECT count(*) FROM e;
-- a magnitude nothing else has is inserted
INSERT INTO e VALUES(4,-8,'neg8') ON CONFLICT(abs(a)) DO UPDATE SET note=excluded.note;
SELECT id,a,note FROM e ORDER BY id;
-- DO UPDATE may change the indexed expression's input, and the index follows
INSERT INTO e VALUES(5,3,'x') ON CONFLICT(abs(a)) DO UPDATE SET a=-30, note='moved';
SELECT id,a,note FROM e ORDER BY id;
SELECT id FROM e WHERE abs(a)=30 ORDER BY id;
SELECT count(*) FROM e WHERE abs(a)=3;
-- and DO NOTHING with the same target skips a colliding row
INSERT INTO e VALUES(6,8,'dup') ON CONFLICT(abs(a)) DO NOTHING;
SELECT count(*) FROM e;
PRAGMA integrity_check;
