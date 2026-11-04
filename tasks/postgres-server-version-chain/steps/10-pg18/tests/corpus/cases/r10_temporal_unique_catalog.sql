CREATE TABLE r10tu3(id int4range, valid_at daterange, x int, UNIQUE (id, valid_at WITHOUT OVERLAPS));
SELECT conname, contype, conperiod FROM pg_constraint WHERE conrelid = 'r10tu3'::regclass ORDER BY conname;
SELECT pg_get_constraintdef(oid) FROM pg_constraint WHERE conrelid = 'r10tu3'::regclass AND contype = 'u';
SELECT indexdef FROM pg_indexes WHERE tablename = 'r10tu3' ORDER BY indexname;
SELECT constraint_type FROM information_schema.table_constraints WHERE table_name = 'r10tu3' ORDER BY 1;
DROP TABLE r10tu3;
