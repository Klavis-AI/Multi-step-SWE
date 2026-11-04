-- R05.5: BEGIN DEFERRED opens a transaction this engine accepts, and the rows
-- written inside it are all present once COMMIT has returned.
--
-- What is observed is the statement form and the visibility of its writes
-- afterwards, in one connection. The lock-timing half of a deferred
-- transaction is not observable at this checkpoint and is not in R05.5; see
-- TEST_INTERFACE.md, "What this milestone does not include".
--
-- The closing aggregate is count() with min() and max() rather than
-- group_concat(): over the same two rows it names the same set of values, and
-- it does so without depending on the order an aggregate visits rows in, which
-- lang_aggfunc.html calls arbitrary unless an ORDER BY argument is given. That
-- argument is not usable here -- at this checkpoint the engine parses
-- `group_concat(v ORDER BY v)` and ignores the ORDER BY, returning rows in
-- rowid order where stock sqlite3 3.50.2 sorts them -- so writing it would
-- have pinned that disagreement instead of removing the dependency.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
BEGIN DEFERRED;
INSERT INTO t VALUES(1,'a'),(2,'b');
COMMIT;
SELECT id,v FROM t ORDER BY id;
SELECT count(*), min(v), max(v) FROM t;
