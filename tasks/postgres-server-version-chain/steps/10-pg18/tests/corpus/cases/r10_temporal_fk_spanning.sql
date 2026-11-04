CREATE TABLE r10tf2_p(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tf2_p VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01')),
                            (int4range(1,2), daterange('2020-06-01','2021-01-01'));
CREATE TABLE r10tf2_c(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf2_p (id, PERIOD valid_at));
INSERT INTO r10tf2_c VALUES (1, daterange('2020-03-01','2020-09-01'), int4range(1,2));
INSERT INTO r10tf2_c VALUES (2, daterange('2020-12-01','2021-02-01'), int4range(1,2));
INSERT INTO r10tf2_c VALUES (3, 'empty'::daterange, int4range(9,9));
SELECT id, valid_at FROM r10tf2_c ORDER BY id;
DROP TABLE r10tf2_c;
DROP TABLE r10tf2_p;
