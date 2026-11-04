CREATE TABLE r10ro2(id int primary key, v int);
INSERT INTO r10ro2 VALUES (1,10),(2,20);
DELETE FROM r10ro2 WHERE id = 1 RETURNING old.v, new.v;
DELETE FROM r10ro2 WHERE id = 2 RETURNING old.*, new.id IS NULL AS new_is_null;
SELECT count(*) FROM r10ro2;
DROP TABLE r10ro2;
