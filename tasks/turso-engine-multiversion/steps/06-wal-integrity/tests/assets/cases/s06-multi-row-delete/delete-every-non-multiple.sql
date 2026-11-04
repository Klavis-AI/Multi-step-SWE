-- R06.14: a predicate DELETE with no row skipped because the tree moved
-- underneath the scan.
--
-- Fifteen of twenty rows match the predicate. A scan that lets the tree move
-- underneath it skips the row after each deletion and leaves ten behind.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'r1'),(2,'r2'),(3,'r3'),(4,'r4'),(5,'r5'),(6,'r6'),(7,'r7'),(8,'r8'),(9,'r9'),(10,'r10'),(11,'r11'),(12,'r12'),(13,'r13'),(14,'r14'),(15,'r15'),(16,'r16'),(17,'r17'),(18,'r18'),(19,'r19'),(20,'r20');
DELETE FROM t WHERE id%4<>0;
SELECT count(*), sum(id) FROM t;
SELECT id FROM t ORDER BY id;
