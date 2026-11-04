CREATE TABLE r10tu1(id int4range, valid_at daterange, x int, UNIQUE (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tu1 VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'), 1);
INSERT INTO r10tu1 VALUES (int4range(1,2), daterange('2020-06-01','2021-01-01'), 2);
INSERT INTO r10tu1 VALUES (int4range(1,2), daterange('2020-02-01','2020-03-01'), 3);
SELECT id, valid_at, x FROM r10tu1 ORDER BY x;
DROP TABLE r10tu1;
