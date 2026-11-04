CREATE TABLE r10tf1_p(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tf1_p VALUES (int4range(1,2), daterange('2020-01-01','2021-01-01'));
CREATE TABLE r10tf1_c(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf1_p (id, PERIOD valid_at));
INSERT INTO r10tf1_c VALUES (1, daterange('2020-02-01','2020-04-01'), int4range(1,2));
INSERT INTO r10tf1_c VALUES (2, daterange('2019-01-01','2019-02-01'), int4range(1,2));
INSERT INTO r10tf1_c VALUES (3, daterange('2020-02-01','2020-04-01'), int4range(9,10));
SELECT id, valid_at, pid FROM r10tf1_c ORDER BY id;
DROP TABLE r10tf1_c;
DROP TABLE r10tf1_p;
