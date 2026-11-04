-- PROFILE.md §1 / lang_createtable.html: DEFAULT may be a parenthesised
-- constant expression, evaluated once per inserted row.
CREATE TABLE t(a INTEGER PRIMARY KEY, b INTEGER DEFAULT (1+2), c DEFAULT (3*4));
INSERT INTO t(a) VALUES(10);
INSERT INTO t DEFAULT VALUES;
SELECT a, b, c FROM t ORDER BY a;
