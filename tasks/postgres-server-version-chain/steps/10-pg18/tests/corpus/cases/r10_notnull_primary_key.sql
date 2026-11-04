CREATE TABLE r10nn6(a int PRIMARY KEY, b int);
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'r10nn6'::regclass ORDER BY conname;
SELECT attname, attnotnull FROM pg_attribute WHERE attrelid = 'r10nn6'::regclass AND attnum > 0 ORDER BY attnum;
ALTER TABLE r10nn6 DROP CONSTRAINT r10nn6_pkey;
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'r10nn6'::regclass ORDER BY conname;
SELECT attname, attnotnull FROM pg_attribute WHERE attrelid = 'r10nn6'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE r10nn6;
