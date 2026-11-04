CREATE TABLE sonr(i int primary key, v int);
INSERT INTO sonr VALUES (1,1);
UPDATE sonr SET v=2 WHERE i=1 RETURNING old.v, new.v;
DROP TABLE sonr;
