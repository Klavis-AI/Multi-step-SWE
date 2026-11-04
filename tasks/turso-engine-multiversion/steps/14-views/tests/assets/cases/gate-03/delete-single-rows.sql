-- R03.13: DELETE ... WHERE taking one row at a time -- from the middle of the
-- table, from the front and from the end -- removing exactly that row and no
-- other, with changes() reporting how many it actually took.
-- DELETE removing one row at a time, chosen by rowid, from the front of the
-- table, from the middle and from the end. After each one the surviving rows
-- are listed in full, so a DELETE that takes the wrong row and a DELETE that
-- takes more than one are both visible immediately, and changes() reports how
-- many rows it actually took.
CREATE TABLE t(id INTEGER PRIMARY KEY, grp TEXT, n INTEGER);
INSERT INTO t VALUES(1,'a',10);
INSERT INTO t VALUES(2,'b',20);
INSERT INTO t VALUES(3,'a',30);
INSERT INTO t VALUES(4,'b',40);
INSERT INTO t VALUES(5,'c',50);
DELETE FROM t WHERE id = 3;
SELECT 'middle', changes();
SELECT id, grp, n FROM t ORDER BY id;
DELETE FROM t WHERE id = 1;
SELECT 'front', changes();
SELECT id, grp, n FROM t ORDER BY id;
DELETE FROM t WHERE id = 5;
SELECT 'end', changes();
SELECT id, grp, n FROM t ORDER BY id;
SELECT count(*), sum(n), min(id), max(id) FROM t;
SELECT count(*) FROM t WHERE grp = 'b';
SELECT id FROM t ORDER BY id DESC;
