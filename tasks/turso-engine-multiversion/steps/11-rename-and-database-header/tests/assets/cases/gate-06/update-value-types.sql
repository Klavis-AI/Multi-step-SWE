-- R06.13: UPDATE changes the storage class held in an untyped column,
-- including to and from NULL, and typeof() follows the new value.
--
-- UPDATE changes a column's stored type, including to and from NULL, and the
-- type tags must follow the values.
CREATE TABLE t(id INTEGER PRIMARY KEY, v);
INSERT INTO t VALUES(1,'text'),(2,42),(3,NULL);
UPDATE t SET v=NULL WHERE id=1;
UPDATE t SET v=2.5 WHERE id=2;
UPDATE t SET v='now set' WHERE id=3;
SELECT id, typeof(v), coalesce(CAST(v AS TEXT),'<null>') FROM t ORDER BY id;
SELECT count(*) FROM t WHERE v IS NULL;
