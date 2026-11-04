CREATE TABLE x07_25a(a int PRIMARY KEY, b text, c int);
CREATE PUBLICATION x07_25p FOR TABLE x07_25a (a, b);
SELECT pubname, tablename, attnames FROM pg_publication_tables
  WHERE pubname='x07_25p' ORDER BY tablename;
DROP PUBLICATION x07_25p;
DROP TABLE x07_25a;
