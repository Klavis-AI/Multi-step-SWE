-- R06.14: a predicate DELETE removes exactly the matching rows and the
-- non-adjacent survivors remain.
--
-- Ten of twelve rows go, and the two survivors are not adjacent.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'r1'),(2,'r2'),(3,'r3'),(4,'r4'),(5,'r5'),(6,'r6'),(7,'r7'),(8,'r8'),(9,'r9'),(10,'r10'),(11,'r11'),(12,'r12');
DELETE FROM t WHERE id<>3 AND id<>7;
SELECT count(*), sum(id) FROM t;
SELECT id,v FROM t ORDER BY id;
