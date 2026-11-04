-- R01.3: changes() and last_insert_rowid() after each statement.
--
-- The requirement asks for the same values stock sqlite3 returns for the same
-- script, and this case is the direct reading of it: the two functions are
-- selected after an explicit rowid, after a second explicit rowid out of
-- order, after an omitted rowid the engine has to allocate, and then a second
-- time with no intervening INSERT -- which is where an engine that resets
-- `changes()` on the wrong event disagrees. Replayed against stock sqlite3
-- automatically, so both values are checked against a second implementation
-- rather than against a frozen guess.
-- changes() and last_insert_rowid() after each single-row INSERT. An engine
-- that forgets to reset the counter, or that reports the rowid of the wrong
-- row, disagrees on one of these lines.
CREATE TABLE t(id INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'x');
SELECT changes(), last_insert_rowid();
INSERT INTO t VALUES(7,'y');
SELECT changes(), last_insert_rowid();
INSERT INTO t(b) VALUES('z');
SELECT changes(), last_insert_rowid();
SELECT changes(), last_insert_rowid();
SELECT count(*) FROM t;
SELECT id, b FROM t ORDER BY id;
