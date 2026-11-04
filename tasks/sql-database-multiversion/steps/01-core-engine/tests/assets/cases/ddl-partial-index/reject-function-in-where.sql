CREATE TABLE t(a, b);
CREATE INDEX bad ON t(a) WHERE length(b) > 2;
SELECT count(*) FROM sqlite_master WHERE type = 'index';
