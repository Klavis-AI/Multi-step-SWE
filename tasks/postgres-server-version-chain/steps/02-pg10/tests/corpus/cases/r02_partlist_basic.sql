CREATE TABLE q2_plb(a int, g text) PARTITION BY LIST (g);
CREATE TABLE q2_plb_x PARTITION OF q2_plb FOR VALUES IN ('x','xx');
CREATE TABLE q2_plb_y PARTITION OF q2_plb FOR VALUES IN ('y');
INSERT INTO q2_plb VALUES (1,'x'),(2,'xx'),(3,'y');
SELECT tableoid::regclass::text AS part, a, g FROM q2_plb ORDER BY a;
INSERT INTO q2_plb VALUES (4,'z');
SELECT count(*) FROM q2_plb;
DROP TABLE q2_plb;
