CREATE TABLE jt(id int, parent int, t text);
INSERT INTO jt VALUES (1,NULL,'root'),(2,1,'a'),(3,1,'b'),(4,2,'c');
SELECT c.t AS child, p.t AS parent FROM jt c LEFT JOIN jt p ON c.parent=p.id ORDER BY c.id;
SELECT t FROM jt WHERE id IN (SELECT parent FROM jt WHERE parent IS NOT NULL) ORDER BY t;
SELECT t, (SELECT count(*) FROM jt x WHERE x.parent=jt.id) AS kids FROM jt ORDER BY id;
SELECT t FROM jt WHERE EXISTS (SELECT 1 FROM jt y WHERE y.parent=jt.id) ORDER BY t;
SELECT * FROM jt a, LATERAL (SELECT count(*) c FROM jt b WHERE b.parent=a.id) s ORDER BY a.id;
DROP TABLE jt;
