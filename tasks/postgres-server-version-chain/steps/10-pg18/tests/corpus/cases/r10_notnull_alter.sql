CREATE TABLE r10nn3(a int, b int);
SELECT count(*) FROM pg_constraint WHERE conrelid = 'r10nn3'::regclass AND contype = 'n';
ALTER TABLE r10nn3 ALTER COLUMN a SET NOT NULL;
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'r10nn3'::regclass ORDER BY conname;
ALTER TABLE r10nn3 ALTER COLUMN a DROP NOT NULL;
SELECT count(*) FROM pg_constraint WHERE conrelid = 'r10nn3'::regclass AND contype = 'n';
DROP TABLE r10nn3;
