CREATE TABLE r08ij1(id int, doc text CHECK (doc IS JSON));
INSERT INTO r08ij1 VALUES (1,'{"a":1}');
INSERT INTO r08ij1 VALUES (2,'oops');
INSERT INTO r08ij1 VALUES (3,NULL);
SELECT id, doc FROM r08ij1 ORDER BY id;
SELECT pg_get_constraintdef(oid) FROM pg_constraint WHERE conrelid='r08ij1'::regclass AND contype='c';
DROP TABLE r08ij1;
