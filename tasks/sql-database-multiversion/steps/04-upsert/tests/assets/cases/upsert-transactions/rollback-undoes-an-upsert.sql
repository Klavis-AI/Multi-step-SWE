-- PROFILE.md 4.8: an upsert is an ordinary write, so step 1 section 7 applies
-- to it -- a rolled-back transaction leaves no trace.
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two');
BEGIN;
INSERT INTO t VALUES(1,'ONE') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
INSERT INTO t VALUES(3,'three') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
INSERT INTO t VALUES(2,'nope') ON CONFLICT(k) DO NOTHING;
SELECT k,c FROM t ORDER BY k;
ROLLBACK;
SELECT k,c FROM t ORDER BY k;
-- the same statements committed
BEGIN;
INSERT INTO t VALUES(1,'ONE') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
INSERT INTO t VALUES(3,'three') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
COMMIT;
SELECT k,c FROM t ORDER BY k;
PRAGMA integrity_check;
