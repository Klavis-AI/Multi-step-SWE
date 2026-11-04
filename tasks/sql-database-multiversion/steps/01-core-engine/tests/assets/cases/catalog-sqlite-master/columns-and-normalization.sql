create   table   t1(a INTEGER, b TEXT);
create index i1 on t1(b);
CREATE UNIQUE INDEX i2 ON t1(a);
SELECT type, name, tbl_name FROM sqlite_master ORDER BY type, name;
SELECT sql FROM sqlite_master WHERE name = 't1';
SELECT sql FROM sqlite_master WHERE name = 'i1';
SELECT sql FROM sqlite_master WHERE name = 'i2';
SELECT count(*) FROM sqlite_master WHERE rootpage > 0;
