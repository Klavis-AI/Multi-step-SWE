CREATE TABLE q4_isd(a int, b bigint);
INSERT INTO q4_isd VALUES ('x', 1);
INSERT INTO q4_isd VALUES (1, 'y');
UPDATE q4_isd SET a = 'z';
SELECT count(*) FROM q4_isd;
DROP TABLE q4_isd;
