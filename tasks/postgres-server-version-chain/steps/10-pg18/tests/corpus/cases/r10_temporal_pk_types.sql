CREATE TABLE r10tp5(id int, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tp6(id int4range, valid_at int, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tp7(valid_at daterange, PRIMARY KEY (valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tp8(id int4range, valid_at daterange, PRIMARY KEY (valid_at WITHOUT OVERLAPS, id));
DROP TABLE IF EXISTS r10tp5;
DROP TABLE IF EXISTS r10tp6;
DROP TABLE IF EXISTS r10tp7;
DROP TABLE IF EXISTS r10tp8;
