CREATE TABLE r10tu2(id int4range, valid_at daterange, x int, UNIQUE (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tu2 VALUES (NULL, daterange('2020-01-01','2020-06-01'), 1);
INSERT INTO r10tu2 VALUES (NULL, daterange('2020-01-01','2020-06-01'), 2);
INSERT INTO r10tu2 VALUES (int4range(1,2), NULL, 3);
SELECT x FROM r10tu2 ORDER BY x;
DROP TABLE r10tu2;
