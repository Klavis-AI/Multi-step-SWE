CREATE TABLE x05_31a(a int, b int GENERATED ALWAYS AS (a*2) STORED, c text);
INSERT INTO x05_31a(a,c) VALUES (1,'x'),(2,'y');
SELECT a, b, c FROM x05_31a ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute
  WHERE attrelid='x05_31a'::regclass AND attnum>0 ORDER BY attnum;
ALTER TABLE x05_31a ALTER COLUMN b DROP EXPRESSION;
SELECT attname, attgenerated FROM pg_attribute
  WHERE attrelid='x05_31a'::regclass AND attnum>0 ORDER BY attnum;
SELECT a, b, c FROM x05_31a ORDER BY a;
DROP TABLE x05_31a;
