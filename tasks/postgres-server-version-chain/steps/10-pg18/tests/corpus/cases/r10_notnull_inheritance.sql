CREATE TABLE r10nn7_p(a int NOT NULL, b int);
CREATE TABLE r10nn7_c() INHERITS (r10nn7_p);
SELECT c.relname, con.conname, con.contype, con.coninhcount, con.conislocal
  FROM pg_constraint con JOIN pg_class c ON c.oid = con.conrelid
  WHERE c.relname LIKE 'r10nn7%' AND con.contype = 'n' ORDER BY c.relname, con.conname;
INSERT INTO r10nn7_c VALUES (NULL, 1);
DROP TABLE r10nn7_c;
DROP TABLE r10nn7_p;
