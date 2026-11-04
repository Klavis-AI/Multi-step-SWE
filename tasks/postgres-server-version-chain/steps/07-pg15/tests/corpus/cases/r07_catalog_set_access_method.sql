CREATE TABLE x07_74a(a int);
INSERT INTO x07_74a VALUES (1),(2);
SELECT c.relname, am.amname FROM pg_class c JOIN pg_am am ON am.oid = c.relam
  WHERE c.relname='x07_74a';
ALTER TABLE x07_74a SET ACCESS METHOD heap;
SELECT count(*) FROM x07_74a;
ALTER TABLE x07_74a SET ACCESS METHOD nosuchmethod;
DROP TABLE x07_74a;
