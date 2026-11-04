CREATE TABLE r10ra1(id int primary key, v int);
INSERT INTO r10ra1 VALUES (1,10);
UPDATE r10ra1 SET v = 11 WHERE id = 1 RETURNING WITH (OLD AS o, NEW AS n) o.v, n.v;
UPDATE r10ra1 SET v = 12 WHERE id = 1 RETURNING WITH (NEW AS n, OLD AS o) n.v - o.v AS delta;
SELECT id, v FROM r10ra1 ORDER BY id;
DROP TABLE r10ra1;
