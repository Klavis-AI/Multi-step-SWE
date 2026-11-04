CREATE TABLE q2_pct_a(i int primary key);
CREATE TABLE q2_pct_b(i int primary key);
CREATE PUBLICATION q2_pct_p FOR TABLE q2_pct_a, q2_pct_b;
SELECT pubname, schemaname, tablename FROM pg_publication_tables
  WHERE pubname = 'q2_pct_p' ORDER BY tablename;
SELECT count(*) FROM pg_publication_rel r JOIN pg_publication p ON p.oid = r.prpubid
  WHERE p.pubname = 'q2_pct_p';
DROP TABLE q2_pct_b;
SELECT tablename FROM pg_publication_tables WHERE pubname = 'q2_pct_p' ORDER BY tablename;
DROP PUBLICATION q2_pct_p;
DROP TABLE q2_pct_a;
