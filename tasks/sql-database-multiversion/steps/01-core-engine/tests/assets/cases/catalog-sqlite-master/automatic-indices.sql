CREATE TABLE t(a TEXT UNIQUE, b TEXT UNIQUE, c);
SELECT type, name, tbl_name, sql IS NULL FROM sqlite_master ORDER BY name;
CREATE TABLE p(x TEXT PRIMARY KEY, y);
SELECT name, sql IS NULL FROM sqlite_master WHERE tbl_name = 'p' AND type = 'index';
CREATE TABLE q(x INTEGER PRIMARY KEY, y);
SELECT count(*) FROM sqlite_master WHERE tbl_name = 'q' AND type = 'index';
