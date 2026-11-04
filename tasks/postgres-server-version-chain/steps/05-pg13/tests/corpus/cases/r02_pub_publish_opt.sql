CREATE TABLE q2_ppo(i int primary key);
CREATE PUBLICATION q2_ppo_ins FOR TABLE q2_ppo WITH (publish = 'insert');
CREATE PUBLICATION q2_ppo_all FOR TABLE q2_ppo WITH (publish = 'insert, update, delete');
CREATE PUBLICATION q2_ppo_none FOR TABLE q2_ppo WITH (publish = '');
SELECT pubname, pubinsert, pubupdate, pubdelete FROM pg_publication
  WHERE pubname IN ('q2_ppo_ins','q2_ppo_all','q2_ppo_none') ORDER BY pubname;
DROP PUBLICATION q2_ppo_ins, q2_ppo_all, q2_ppo_none;
DROP TABLE q2_ppo;
