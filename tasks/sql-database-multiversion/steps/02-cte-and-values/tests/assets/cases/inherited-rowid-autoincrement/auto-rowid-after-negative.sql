-- autoinc.html: "The usual algorithm is to give the newly created row a ROWID
-- that is one larger than the largest ROWID in the table prior to the insert.
-- If the table is initially empty, then a ROWID of 1 is used." and "If no
-- negative ROWID values are inserted explicitly, then automatically generated
-- ROWID values will always be greater than zero."  So once a negative rowid
-- has been inserted explicitly, "one larger than the largest" is still the
-- rule and the next automatic rowid is negative too.  PROFILE.md section 2
-- puts INTEGER PRIMARY KEY as an alias for the rowid, and autoinc.html, in
-- scope.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(-5, 'x');
INSERT INTO t VALUES(NULL, 'y');
INSERT INTO t(b) VALUES('z');
SELECT a, b FROM t ORDER BY a;
-- the same rule reached through the "rowid" name on a table with no alias
CREATE TABLE u(a, b);
INSERT INTO u(rowid, a, b) VALUES(-3, 1, 'p');
INSERT INTO u(a, b) VALUES(2, 'q');
SELECT rowid, a, b FROM u ORDER BY rowid;
