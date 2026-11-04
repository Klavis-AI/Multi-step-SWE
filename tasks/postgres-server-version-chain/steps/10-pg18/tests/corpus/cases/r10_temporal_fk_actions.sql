CREATE TABLE r10tf5_p(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tf5_c1(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf5_p ON DELETE CASCADE);
CREATE TABLE r10tf5_c2(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf5_p ON UPDATE CASCADE);
CREATE TABLE r10tf5_c3(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf5_p ON DELETE NO ACTION);
SELECT count(*) FROM pg_class WHERE relname LIKE 'r10tf5_c%';
DROP TABLE IF EXISTS r10tf5_c1;
DROP TABLE IF EXISTS r10tf5_c2;
DROP TABLE IF EXISTS r10tf5_c3;
DROP TABLE r10tf5_p;
