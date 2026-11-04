CREATE TABLE x05_35a(a int, b int GENERATED ALWAYS AS (a*3) STORED);
SELECT column_name, is_generated, generation_expression
  FROM information_schema.columns WHERE table_name='x05_35a' ORDER BY ordinal_position;
SELECT pg_get_expr(adbin, adrelid) FROM pg_attrdef
  WHERE adrelid='x05_35a'::regclass ORDER BY adnum;
ALTER TABLE x05_35a ALTER COLUMN b DROP EXPRESSION;
SELECT column_name, is_generated, generation_expression
  FROM information_schema.columns WHERE table_name='x05_35a' ORDER BY ordinal_position;
SELECT count(*) FROM pg_attrdef WHERE adrelid='x05_35a'::regclass;
DROP TABLE x05_35a;
