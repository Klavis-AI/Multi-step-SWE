CREATE TABLE q3_cvl(a int, b int);
CREATE INDEX q3_cvl_hash ON q3_cvl USING hash (a) INCLUDE (b);
CREATE INDEX q3_cvl_gin ON q3_cvl USING gin (a) INCLUDE (b);
CREATE INDEX q3_cvl_missing ON q3_cvl (a) INCLUDE (nosuchcol);
CREATE INDEX q3_cvl_ok ON q3_cvl (a) INCLUDE (b);
CREATE UNIQUE INDEX q3_cvl_dup ON q3_cvl (a) INCLUDE (a);
DROP TABLE q3_cvl;
