CREATE TABLE r10nn4(a int, b int, CONSTRAINT r10nn4_a_nn NOT NULL a);
INSERT INTO r10nn4 VALUES (NULL, 1);
SELECT conname, contype, convalidated FROM pg_constraint WHERE conrelid = 'r10nn4'::regclass ORDER BY conname;
ALTER TABLE r10nn4 DROP CONSTRAINT r10nn4_a_nn;
INSERT INTO r10nn4 VALUES (NULL, 2);
SELECT count(*) FROM r10nn4;
DROP TABLE r10nn4;
