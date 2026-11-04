CREATE TABLE sc(a int, b int);
CREATE UNIQUE INDEX sc_ix ON sc(a) INCLUDE (b);
SELECT indexdef FROM pg_indexes WHERE indexname='sc_ix';
DROP TABLE sc;
