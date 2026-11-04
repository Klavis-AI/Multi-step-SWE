CREATE TABLE r10tu8(a int4range, b int4range, valid_at daterange, UNIQUE (a, b, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tu8 VALUES (int4range(1,2), int4range(1,2), daterange('2020-01-01','2020-06-01'));
INSERT INTO r10tu8 VALUES (int4range(1,2), int4range(2,3), daterange('2020-01-01','2020-06-01'));
INSERT INTO r10tu8 VALUES (int4range(1,2), int4range(1,2), daterange('2020-02-01','2020-03-01'));
SELECT a, b, valid_at FROM r10tu8 ORDER BY a, b, valid_at;
DROP TABLE r10tu8;
