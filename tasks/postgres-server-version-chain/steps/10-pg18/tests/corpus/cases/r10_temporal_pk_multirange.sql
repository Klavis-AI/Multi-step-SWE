CREATE TABLE r10tp9(id int4range, valid_at datemultirange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tp9 VALUES (int4range(1,2), datemultirange(daterange('2020-01-01','2020-02-01')));
INSERT INTO r10tp9 VALUES (int4range(1,2), datemultirange(daterange('2020-03-01','2020-04-01')));
INSERT INTO r10tp9 VALUES (int4range(1,2), datemultirange(daterange('2020-01-15','2020-01-20')));
SELECT id, valid_at FROM r10tp9 ORDER BY id, valid_at;
DROP TABLE r10tp9;
