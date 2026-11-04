CREATE TABLE x07_17a(a int PRIMARY KEY, b text, c int);
CREATE PUBLICATION x07_17p FOR TABLE x07_17a WHERE (a > 10);
SELECT pubname, schemaname, tablename, rowfilter
  FROM pg_publication_tables WHERE pubname='x07_17p' ORDER BY tablename;
DROP PUBLICATION x07_17p;
DROP TABLE x07_17a;
