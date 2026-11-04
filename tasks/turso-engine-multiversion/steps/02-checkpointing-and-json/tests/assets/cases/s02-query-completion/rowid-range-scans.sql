-- R02.13: seeking a range of rowids rather than a single one: open-ended
-- above, open
-- ended below, BETWEEN in both directions, and a set membership test. Each
-- range is answered from the same five-row table and has to agree with the
-- aggregate taken over the same range, so an off-by-one at either end of a
-- range shows up twice. A range whose ends cross selects nothing and is
-- counted as well.
CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER, s TEXT);
INSERT INTO t VALUES(1,10,'aa');
INSERT INTO t VALUES(2,20,'ab');
INSERT INTO t VALUES(3,30,'bc');
INSERT INTO t VALUES(4,40,'bd');
INSERT INTO t VALUES(5,50,'ce');
SELECT 'gt', id, v FROM t WHERE id > 2 ORDER BY id;
SELECT 'gt count', count(*), sum(v) FROM t WHERE id > 2;
SELECT 'ge', id, v FROM t WHERE id >= 2 ORDER BY id;
SELECT 'le', id, v FROM t WHERE id <= 2 ORDER BY id;
SELECT 'lt count', count(*), sum(v) FROM t WHERE id < 2;
SELECT 'between', id, v FROM t WHERE id BETWEEN 2 AND 4 ORDER BY id;
SELECT 'between desc', id, v FROM t WHERE id BETWEEN 2 AND 4 ORDER BY id DESC;
SELECT 'in list', id FROM t WHERE id IN (1,3,5) ORDER BY id;
SELECT 'not a rowid', id, v FROM t WHERE v > 25 ORDER BY id;
SELECT count(*) FROM t WHERE id BETWEEN 4 AND 2;
SELECT 'whole table', count(*), sum(v), min(id), max(id) FROM t;
