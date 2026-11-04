CREATE TABLE a(k INTEGER, v TEXT);
CREATE TABLE b(k INTEGER, v TEXT);
CREATE TABLE c(k INTEGER, v TEXT);
INSERT INTO a VALUES(1,'a1'),(2,'a2');
INSERT INTO b VALUES(1,'b1'),(2,'b2');
INSERT INTO c VALUES(2,'c2');
SELECT a.v, b.v, c.v FROM a JOIN b ON a.k = b.k JOIN c ON b.k = c.k ORDER BY a.v;
SELECT a.v, b.v, c.v IS NULL FROM a JOIN b ON a.k = b.k LEFT JOIN c ON b.k = c.k ORDER BY a.v;
