CREATE PUBLICATION q2_pal_p FOR ALL TABLES;
SELECT pubname, puballtables FROM pg_publication WHERE pubname = 'q2_pal_p';
ALTER PUBLICATION q2_pal_p SET (publish = 'insert');
SELECT pubinsert, pubupdate, pubdelete FROM pg_publication WHERE pubname = 'q2_pal_p';
DROP PUBLICATION q2_pal_p;
CREATE PUBLICATION q2_pal_q FOR ALL TABLES WITH (publish = 'delete');
SELECT puballtables, pubinsert, pubdelete FROM pg_publication WHERE pubname = 'q2_pal_q';
DROP PUBLICATION q2_pal_q;
