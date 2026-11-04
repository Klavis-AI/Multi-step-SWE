-- datatype3.html §3.2: "if X and Y.Z are column names, then +X and +Y.Z are
-- considered expressions for the purpose of determining affinity." So +a has
-- NONE affinity even when a is TEXT. §3.3 then applies no conversion, and an
-- INTEGER is less than TEXT.
CREATE TABLE t1(a TEXT);
INSERT INTO t1 VALUES('500');
SELECT +a < 40, +a < 600 FROM t1;
SELECT a < 40, a < 600 FROM t1;
