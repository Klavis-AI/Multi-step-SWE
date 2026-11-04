CREATE TABLE keepme(a);
INSERT INTO keepme VALUES(5);
DROP TABLE nosuch;
SELECT name FROM sqlite_master;
SELECT a FROM keepme;
