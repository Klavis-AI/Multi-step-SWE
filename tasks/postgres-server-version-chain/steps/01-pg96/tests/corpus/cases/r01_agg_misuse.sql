CREATE TABLE am(g int, v int);
INSERT INTO am VALUES (1,1);
SELECT g, v FROM am GROUP BY g;
SELECT count(*) FROM am WHERE count(*) > 0;
DROP TABLE am;
