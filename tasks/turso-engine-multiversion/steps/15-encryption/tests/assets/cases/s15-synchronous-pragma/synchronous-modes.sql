-- R15.14: the synchronous pragma's set form and its query form, the numeric
-- report, the default of 2, and that writes keep working in either mode.
-- The pager gains a selectable synchronous mode at this checkpoint. It
-- defaults to full, takes the usual OFF/FULL spellings and the numeric ones,
-- and reports back the numeric mode it is in. Writes keep working in either
-- mode; only the fsync discipline differs, which is not observable from the
-- shell.
-- Both spellings of both modes are exercised, in the order word, word,
-- number, number, because R15.14 says "OFF/FULL and their numeric spellings
-- both select" and a case that only ever wrote the words would leave half of
-- that sentence ungraded. The query form always answers numerically, so the
-- reply to `= OFF` and the reply to `= 0` are indistinguishable; what the
-- numeric pair adds is that the set form accepted the number at all.
PRAGMA synchronous;
PRAGMA synchronous = OFF;
PRAGMA synchronous;
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'written with synchronous off');
PRAGMA synchronous = FULL;
PRAGMA synchronous;
INSERT INTO t VALUES(2,'written with synchronous full');
PRAGMA synchronous = 0;
PRAGMA synchronous;
INSERT INTO t VALUES(3,'written after the numeric off');
PRAGMA synchronous = 2;
PRAGMA synchronous;
INSERT INTO t VALUES(4,'written after the numeric full');
SELECT id, v FROM t ORDER BY id;
SELECT count(*) FROM t;
