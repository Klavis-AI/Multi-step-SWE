SHOW default_table_access_method;
CREATE TABLE q4_tad(a int);
SELECT c.relname, a.amname FROM pg_class c LEFT JOIN pg_am a ON a.oid = c.relam
  WHERE c.relname = 'q4_tad';
CREATE TABLE q4_tae(a int) USING heap;
SELECT c.relname, a.amname FROM pg_class c LEFT JOIN pg_am a ON a.oid = c.relam
  WHERE c.relname = 'q4_tae';
DROP TABLE IF EXISTS q4_tae;
DROP TABLE IF EXISTS q4_tad;
