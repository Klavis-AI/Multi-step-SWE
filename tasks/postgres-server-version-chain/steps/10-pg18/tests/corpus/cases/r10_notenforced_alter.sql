CREATE TABLE r10ne3(a int, CONSTRAINT r10ne3_chk CHECK (a > 0) NOT ENFORCED);
INSERT INTO r10ne3 VALUES (5);
ALTER TABLE r10ne3 ALTER CONSTRAINT r10ne3_chk ENFORCED;
SELECT conname, conenforced, convalidated FROM pg_constraint WHERE conrelid = 'r10ne3'::regclass ORDER BY conname;
INSERT INTO r10ne3 VALUES (-1);
SELECT a FROM r10ne3 ORDER BY a;
DROP TABLE r10ne3;
