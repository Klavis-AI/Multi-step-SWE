CREATE TABLE r10ra2(id int primary key, v int);
INSERT INTO r10ra2 VALUES (1,10);
UPDATE r10ra2 SET v = 20 WHERE id = 1 RETURNING WITH (OLD AS o) o.v, v;
UPDATE r10ra2 SET v = 30 WHERE id = 1 RETURNING WITH (NEW AS n) n.v, old.v;
DELETE FROM r10ra2 WHERE id = 1 RETURNING WITH (OLD AS prev) prev.id, prev.v;
DROP TABLE r10ra2;
