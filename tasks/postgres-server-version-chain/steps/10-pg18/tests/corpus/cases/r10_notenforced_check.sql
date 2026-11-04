CREATE TABLE r10ne1(a int, CONSTRAINT r10ne1_chk CHECK (a > 0) NOT ENFORCED);
INSERT INTO r10ne1 VALUES (-5),(5);
SELECT conname, contype, conenforced, convalidated FROM pg_constraint WHERE conrelid = 'r10ne1'::regclass ORDER BY conname;
SELECT a FROM r10ne1 ORDER BY a;
DROP TABLE r10ne1;
