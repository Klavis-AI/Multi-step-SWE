-- R06.11: several columns assigned in one UPDATE under a predicate,
-- including one assigned from another column of the same row.
--
-- Several columns assigned at once under a predicate, including a column set
-- from another column of the same row.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, c TEXT);
INSERT INTO t VALUES(1,1,100,'p'),(2,2,200,'q'),(3,3,300,'r'),(4,4,400,'s');
UPDATE t SET a=b, c=upper(c) WHERE b>=200 AND id<4;
SELECT id,a,b,c FROM t ORDER BY id;
SELECT count(*) FROM t WHERE c=upper(c);
-- The "reads the row's pre-update values" half of R06.11, made observable: a
-- simultaneous swap. Row 1 holds a=1, b=100. If the two assignments were
-- applied one after another against the row as it is being written, b=a would
-- read the a that a=b had just stored and both columns would end up 100. Every
-- assignment reading the pre-update row is what makes the answer 100|1.
UPDATE t SET a=b, b=a WHERE id=1;
SELECT id,a,b FROM t WHERE id=1;
