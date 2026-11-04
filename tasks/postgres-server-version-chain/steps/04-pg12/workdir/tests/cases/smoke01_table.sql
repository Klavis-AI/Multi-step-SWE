CREATE TABLE s1(a int primary key, b text);
INSERT INTO s1 VALUES (1,'x'),(2,'y');
SELECT * FROM s1 ORDER BY a;
DROP TABLE s1;
