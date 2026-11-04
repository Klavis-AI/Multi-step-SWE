CREATE TABLE r10tf6_p(id int4range, valid_at daterange);
CREATE TABLE r10tf6_c(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf6_p (id, PERIOD valid_at));
CREATE TABLE r10tf6_d(id int, valid_at int, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf6_p (id, PERIOD valid_at));
DROP TABLE IF EXISTS r10tf6_c;
DROP TABLE IF EXISTS r10tf6_d;
DROP TABLE r10tf6_p;
