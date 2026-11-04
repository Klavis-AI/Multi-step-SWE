CREATE TABLE r10ra3(id int primary key, v int);
INSERT INTO r10ra3 VALUES (1,10);
UPDATE r10ra3 SET v = 11 WHERE id = 1 RETURNING WITH (OLD AS new, NEW AS old) new.v AS was, old.v AS now;
UPDATE r10ra3 SET v = 12 WHERE id = 1 RETURNING WITH (OLD AS r10ra3) r10ra3.v;
SELECT id, v FROM r10ra3 ORDER BY id;
DROP TABLE r10ra3;
