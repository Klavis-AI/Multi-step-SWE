CREATE TABLE x07_31a(a int, b text);
INSERT INTO x07_31a VALUES (1,'x');
CREATE VIEW x07_31v WITH (security_invoker = true) AS SELECT * FROM x07_31a;
CREATE VIEW x07_31w AS SELECT * FROM x07_31a;
SELECT relname, reloptions FROM pg_class
  WHERE relname IN ('x07_31v','x07_31w') ORDER BY relname;
SELECT a, b FROM x07_31v ORDER BY a;
DROP VIEW x07_31v, x07_31w;
DROP TABLE x07_31a;
