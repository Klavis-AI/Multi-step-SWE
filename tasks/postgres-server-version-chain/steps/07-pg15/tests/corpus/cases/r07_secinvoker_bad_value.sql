CREATE TABLE x07_34a(a int);
CREATE VIEW x07_34v WITH (security_invoker = nosuch) AS SELECT * FROM x07_34a;
CREATE VIEW x07_34v WITH (security_invoker) AS SELECT * FROM x07_34a;
SELECT relname, reloptions FROM pg_class WHERE relname='x07_34v';
CREATE TABLE x07_34b(a int) WITH (security_invoker = true);
DROP VIEW IF EXISTS x07_34v;
DROP TABLE x07_34a;
