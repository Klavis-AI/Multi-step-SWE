CREATE TABLE src(t TEXT, n NUMERIC, i INTEGER, r REAL, b BLOB);
INSERT INTO src VALUES('x', 1, 2, 3.5, 'y');
INSERT INTO src VALUES('z', 4, 5, 6.5, 'w');
CREATE TABLE dst AS SELECT t, n, i, r, b FROM src ORDER BY t;
PRAGMA table_info(dst);
SELECT rowid, t, i FROM dst ORDER BY rowid;
