CREATE TABLE x07_36a(a int);
INSERT INTO x07_36a VALUES (1),(2);
CREATE VIEW x07_36v WITH (security_invoker = true) AS SELECT * FROM x07_36a;
CREATE VIEW x07_36w AS SELECT * FROM x07_36v;
SELECT count(*) FROM x07_36w;
SELECT c.relname, c.reloptions FROM pg_class c
  WHERE c.relname IN ('x07_36v','x07_36w') ORDER BY c.relname;
DROP VIEW x07_36w, x07_36v;
DROP TABLE x07_36a;
