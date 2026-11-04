CREATE TABLE q4_ogr(a int);
SELECT relhasoids FROM pg_class WHERE relname = 'q4_ogr';
SELECT count(*) FROM pg_attribute WHERE attrelid = 'pg_class'::regclass AND attname = 'relhasoids';
SELECT attname FROM pg_attribute WHERE attrelid = 'q4_ogr'::regclass AND attnum < 0 ORDER BY attnum;
DROP TABLE q4_ogr;
