CREATE TABLE q2_pba(i int primary key, t text);
CREATE PUBLICATION q2_pba_p1 FOR TABLE q2_pba;
SELECT pubname, puballtables, pubinsert, pubupdate, pubdelete
  FROM pg_publication WHERE pubname = 'q2_pba_p1';
SELECT pubname, schemaname, tablename FROM pg_publication_tables
  WHERE pubname = 'q2_pba_p1' ORDER BY tablename;
DROP PUBLICATION q2_pba_p1;
SELECT count(*) FROM pg_publication WHERE pubname = 'q2_pba_p1';
DROP TABLE q2_pba;
