-- lang_UPSERT.html: "The conflict target is required for DO UPDATE upserts,
-- but is optional for DO NOTHING."  (PROFILE.md 4.7)
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT);
INSERT INTO t VALUES(1,'one');
-- the permitted spellings: DO UPDATE with a target, DO NOTHING without one
INSERT INTO t VALUES(1,'ONE') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
INSERT INTO t VALUES(1,'nope') ON CONFLICT DO NOTHING;
SELECT k,c FROM t ORDER BY k;
-- DO UPDATE with no conflict target is rejected
INSERT INTO t VALUES(1,'X') ON CONFLICT DO UPDATE SET c=excluded.c;
SELECT k,c FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
