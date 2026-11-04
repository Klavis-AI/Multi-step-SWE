CREATE TABLE q3_cvk(a int, b int, c text);
CREATE INDEX q3_cvk_plain ON q3_cvk (a, b);
CREATE INDEX q3_cvk_incl ON q3_cvk (a) INCLUDE (b, c);
SELECT c.relname, i.indnatts, i.indnkeyatts, i.indkey::text FROM pg_index i
  JOIN pg_class c ON c.oid = i.indexrelid
  WHERE c.relname IN ('q3_cvk_plain','q3_cvk_incl') ORDER BY c.relname;
SELECT attname FROM pg_attribute WHERE attrelid = 'q3_cvk_incl'::regclass
  AND attnum > 0 ORDER BY attnum;
DROP TABLE q3_cvk;
