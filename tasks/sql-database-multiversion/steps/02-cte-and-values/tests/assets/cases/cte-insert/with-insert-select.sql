-- lang_with.html: a WITH clause may be prepended to an INSERT statement.
CREATE TABLE src(a INTEGER, b TEXT);
CREATE TABLE dst(x INTEGER, y TEXT);
INSERT INTO src VALUES(1,'p'),(2,'q'),(3,'r');
WITH s(k, v) AS (SELECT a, b FROM src WHERE a >= 2) INSERT INTO dst(x, y) SELECT k, v FROM s;
SELECT x, y FROM dst ORDER BY x;
WITH s(k, v) AS (VALUES(9,'z')) INSERT INTO dst SELECT k, v FROM s;
SELECT count(*), sum(x) FROM dst;
WITH s(k) AS (SELECT a FROM src) INSERT INTO dst(x) SELECT k FROM s WHERE k = 1;
SELECT x, y IS NULL FROM dst WHERE x = 1;
