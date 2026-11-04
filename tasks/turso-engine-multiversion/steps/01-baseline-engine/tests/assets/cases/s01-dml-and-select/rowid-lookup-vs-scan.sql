-- R01.7: a primary-key seek and a full table scan return the same rows.
--
-- R01.7 asks that a primary-key or rowid access path and the equivalent full
-- scan agree value for value over the same data. `id` is the rowid alias, so
-- `WHERE id = 30` and `WHERE rowid = 30` are the seek and `WHERE s = 'thirty'`
-- is the scan; all three name the same row, and the aggregate line reads the
-- whole table a fourth way. The two ORDER BY directions and the two `ifnull`
-- probes -- one key that exists, one that does not -- extend the same
-- agreement to a traversal in each direction and to the empty result.
-- Secondary indexes are not part of this at step 01: CREATE INDEX arrives
-- later in the chain, which is why the requirement is read here as the rowid
-- access path and is said so in `TEST_INTERFACE.md`.
-- A rowid (primary-key) seek and a full table scan filtered on the same
-- predicate must agree, value for value, and agree with the aggregate over
-- the whole table. Only the access path differs.
CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER, s TEXT);
INSERT INTO t VALUES(10,100,'ten');
INSERT INTO t VALUES(20,200,'twenty');
INSERT INTO t VALUES(30,300,'thirty');
INSERT INTO t VALUES(40,400,'forty');
SELECT v, s FROM t WHERE id = 30;
SELECT v, s FROM t WHERE rowid = 30;
SELECT v, s FROM t WHERE s = 'thirty';
SELECT count(*), sum(v), min(id), max(id) FROM t;
SELECT id, v FROM t ORDER BY id;
SELECT id, v FROM t ORDER BY id DESC;
SELECT ifnull(max(v), -1) FROM t WHERE id = 99;
SELECT ifnull(max(v), -1) FROM t WHERE id = 10;
