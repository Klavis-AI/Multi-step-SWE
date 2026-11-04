CREATE TABLE t(a, b);
CREATE INDEX sqlite_mine ON t(a);
SELECT name FROM sqlite_master ORDER BY name;
