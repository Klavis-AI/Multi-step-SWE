CREATE TABLE sto(id int4range, valid_at daterange,
  CONSTRAINT sto_pk PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO sto VALUES ('[1,2)','[2020-01-01,2020-06-01)');
SELECT count(*) FROM sto;
DROP TABLE sto;
