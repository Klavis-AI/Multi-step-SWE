-- R13.12: which images each mode keeps, and when a mode change takes effect.
-- Under `before` an INSERT records neither image and an UPDATE and a DELETE
-- record only the pre-image; under `after` a DELETE records neither and an
-- INSERT and an UPDATE record only the post-image. The second pragma is
-- issued part way through the session, and the mutations after it follow the
-- new mode while the three before it kept the old one.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('before');
INSERT INTO t VALUES(1,'a');
UPDATE t SET v='b' WHERE id=1;
DELETE FROM t WHERE id=1;
PRAGMA unstable_capture_data_changes_conn('after');
INSERT INTO t VALUES(2,'c');
UPDATE t SET v='d' WHERE id=2;
DELETE FROM t WHERE id=2;
SELECT change_id, change_type, typeof(before), typeof(after) FROM turso_cdc ORDER BY change_id;
