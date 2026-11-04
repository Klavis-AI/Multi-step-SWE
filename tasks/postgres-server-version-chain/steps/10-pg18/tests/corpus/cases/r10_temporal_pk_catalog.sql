CREATE TABLE r10tpa(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
SELECT conname, contype, conperiod FROM pg_constraint WHERE conrelid = 'r10tpa'::regclass ORDER BY conname;
SELECT pg_get_constraintdef(oid) FROM pg_constraint WHERE conrelid = 'r10tpa'::regclass AND contype = 'p';
SELECT indexdef FROM pg_indexes WHERE tablename = 'r10tpa' ORDER BY indexname;
SELECT amname FROM pg_class ci JOIN pg_am am ON am.oid = ci.relam WHERE ci.relname = 'r10tpa_pkey';
SELECT constraint_type FROM information_schema.table_constraints WHERE table_name = 'r10tpa' ORDER BY 1;
DROP TABLE r10tpa;
