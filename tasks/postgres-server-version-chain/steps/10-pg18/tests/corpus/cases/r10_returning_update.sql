CREATE TABLE r10ro1(id int primary key, v int, t text);
INSERT INTO r10ro1 VALUES (1,10,'a'),(2,20,'b');
UPDATE r10ro1 SET v = v + 1 WHERE id = 1 RETURNING old.v, new.v, id;
WITH u AS (UPDATE r10ro1 SET v = v * 2 RETURNING id AS id, old.v AS was, new.v AS now)
SELECT id, was, now FROM u ORDER BY id;
SELECT id, v, t FROM r10ro1 ORDER BY id;
DROP TABLE r10ro1;
