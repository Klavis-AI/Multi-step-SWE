-- lang_altertable.html: "The column may not have a default value of
-- CURRENT_TIME, CURRENT_DATE, CURRENT_TIMESTAMP, or an expression in
-- parentheses."
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'p');
ALTER TABLE t ADD COLUMN c INTEGER DEFAULT (1+2);
SELECT a, b FROM t ORDER BY a;
