CREATE TABLE r10ro8(id int primary key, v int);
INSERT INTO r10ro8 VALUES (1,10);
SELECT old.v FROM r10ro8;
UPDATE r10ro8 SET v = 2 WHERE id = 1 RETURNING old.nosuchcol;
UPDATE r10ro8 SET v = 3 WHERE id = 1 RETURNING older.v;
UPDATE r10ro8 SET v = old.v + 1 WHERE id = 1;
DELETE FROM r10ro8 WHERE new.v = 1;
DROP TABLE r10ro8;
