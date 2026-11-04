CREATE TABLE l(a INTEGER, x TEXT);
CREATE TABLE r(a INTEGER, y TEXT);
INSERT INTO l VALUES(1,'l1'),(2,'l2');
INSERT INTO r VALUES(1,'r1'),(1,'r1b'),(3,'r3');
SELECT l.a, x, r.a, y FROM l, r ORDER BY l.a, x, r.a, y;
SELECT l.a, x, y FROM l JOIN r ON l.a = r.a ORDER BY l.a, y;
SELECT l.a, x, y FROM l INNER JOIN r ON l.a = r.a ORDER BY l.a, y;
SELECT l.a, x, y FROM l CROSS JOIN r ON l.a = r.a ORDER BY l.a, y;
