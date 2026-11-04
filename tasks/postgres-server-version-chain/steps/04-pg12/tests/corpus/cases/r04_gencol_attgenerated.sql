CREATE TABLE q4_gcg(a int, b int GENERATED ALWAYS AS (a * 2) STORED, c int DEFAULT 4);
SELECT attname, attgenerated, atthasdef, attnotnull FROM pg_attribute
  WHERE attrelid = 'q4_gcg'::regclass AND attnum > 0 ORDER BY attnum;
SELECT a.attname, pg_get_expr(d.adbin, d.adrelid) FROM pg_attrdef d
  JOIN pg_attribute a ON a.attrelid = d.adrelid AND a.attnum = d.adnum
  WHERE d.adrelid = 'q4_gcg'::regclass ORDER BY a.attname;
DROP TABLE q4_gcg;
