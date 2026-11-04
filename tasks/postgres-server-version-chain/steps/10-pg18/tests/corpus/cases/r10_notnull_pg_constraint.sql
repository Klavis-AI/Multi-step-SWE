CREATE TABLE r10nn1(a int NOT NULL, b int, c text NOT NULL);
SELECT conname, contype, conkey FROM pg_constraint WHERE conrelid = 'r10nn1'::regclass ORDER BY conname;
SELECT count(*) FROM pg_constraint WHERE conrelid = 'r10nn1'::regclass AND contype = 'n';
SELECT attname, attnotnull FROM pg_attribute WHERE attrelid = 'r10nn1'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE r10nn1;
