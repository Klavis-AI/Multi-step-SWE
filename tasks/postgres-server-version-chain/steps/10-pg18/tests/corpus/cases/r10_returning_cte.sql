CREATE TABLE r10ro9(id int primary key, v int);
INSERT INTO r10ro9 VALUES (1,10),(2,20);
WITH u AS (UPDATE r10ro9 SET v = v + 1 RETURNING id, old.v AS ov, new.v AS nv)
SELECT id, ov, nv FROM u ORDER BY id;
WITH d AS (DELETE FROM r10ro9 WHERE id = 1 RETURNING old.id, old.v)
SELECT * FROM d ORDER BY 1;
SELECT id, v FROM r10ro9 ORDER BY id;
DROP TABLE r10ro9;
