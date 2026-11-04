CREATE TABLE sq(i int primary key);
CREATE PUBLICATION sqp FOR TABLE sq;
SELECT pubname FROM pg_publication WHERE pubname='sqp';
DROP PUBLICATION sqp;
DROP TABLE sq;
