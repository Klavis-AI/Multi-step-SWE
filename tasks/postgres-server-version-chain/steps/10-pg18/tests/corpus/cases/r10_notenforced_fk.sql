CREATE TABLE r10ne2_p(id int PRIMARY KEY);
CREATE TABLE r10ne2_c(id int, pid int, CONSTRAINT r10ne2_fk FOREIGN KEY (pid) REFERENCES r10ne2_p(id) NOT ENFORCED);
INSERT INTO r10ne2_c VALUES (1, 99);
SELECT conname, contype, conenforced, convalidated FROM pg_constraint WHERE conrelid = 'r10ne2_c'::regclass ORDER BY conname;
SELECT pg_get_constraintdef(oid) FROM pg_constraint WHERE conname = 'r10ne2_fk';
DROP TABLE r10ne2_c;
DROP TABLE r10ne2_p;
