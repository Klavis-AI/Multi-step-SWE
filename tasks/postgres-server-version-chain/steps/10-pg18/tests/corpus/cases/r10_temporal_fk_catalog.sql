CREATE TABLE r10tf4_p(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tf4_c(id int, valid_at daterange, pid int4range,
   CONSTRAINT r10tf4_fk FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf4_p (id, PERIOD valid_at));
SELECT conname, contype, conperiod, confupdtype, confdeltype FROM pg_constraint
  WHERE conrelid = 'r10tf4_c'::regclass ORDER BY conname;
SELECT pg_get_constraintdef(oid) FROM pg_constraint WHERE conname = 'r10tf4_fk';
SELECT constraint_type FROM information_schema.table_constraints WHERE table_name = 'r10tf4_c' ORDER BY 1;
DROP TABLE r10tf4_c;
DROP TABLE r10tf4_p;
