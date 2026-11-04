-- R14.3: a view over a join returns the same rows in the same order as the join written inline.
-- A view whose body is a join expands to the same join, with the same row
-- order under the same ORDER BY, as the join written inline.
CREATE TABLE cust(id INTEGER PRIMARY KEY, name TEXT);
CREATE TABLE ord(oid INTEGER PRIMARY KEY, cid INTEGER, amt INTEGER);
INSERT INTO cust VALUES(1,'ana'),(2,'bo'),(3,'cy');
INSERT INTO ord VALUES(10,1,5),(11,1,7),(12,2,9);
CREATE VIEW placed AS SELECT cust.name AS name, ord.oid AS oid, ord.amt AS amt FROM cust JOIN ord ON ord.cid = cust.id;
SELECT name, oid, amt FROM placed ORDER BY oid;
SELECT cust.name, ord.oid, ord.amt FROM cust JOIN ord ON ord.cid = cust.id ORDER BY ord.oid;
SELECT count(*) FROM placed;
