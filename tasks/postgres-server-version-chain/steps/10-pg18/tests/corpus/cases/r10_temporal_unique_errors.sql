CREATE TABLE r10tu4(id int, valid_at daterange, UNIQUE (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tu5(id int4range, valid_at text, UNIQUE (id, valid_at WITHOUT OVERLAPS));
CREATE TABLE r10tu6(id int4range, valid_at daterange, UNIQUE (id, valid_at WITHOUT OVERLAPS) WITH (fillfactor = 0));
DROP TABLE IF EXISTS r10tu4;
DROP TABLE IF EXISTS r10tu5;
DROP TABLE IF EXISTS r10tu6;
