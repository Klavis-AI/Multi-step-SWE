CREATE TABLE x07_19a(a int PRIMARY KEY, b text);
CREATE PUBLICATION x07_19p FOR TABLE x07_19a WHERE (a > 5);
SELECT pg_get_expr(r.prqual, r.prrelid) FROM pg_publication_rel r
  JOIN pg_publication p ON p.oid = r.prpubid WHERE p.pubname='x07_19p';
SELECT count(*) FROM pg_attribute WHERE attrelid='pg_publication_rel'::regclass
  AND attname IN ('prqual','prattrs');
SELECT attname FROM pg_attribute
  WHERE attrelid='pg_publication_tables'::regclass AND attnum>0 ORDER BY attnum;
DROP PUBLICATION x07_19p;
DROP TABLE x07_19a;
