CREATE TABLE ism_p(i int primary key);
CREATE TABLE ism_c(i int REFERENCES ism_p(i) ON DELETE CASCADE, j int CHECK (j > 0));
CREATE VIEW ism_v AS SELECT i FROM ism_p;
CREATE SEQUENCE ism_s;
SELECT constraint_type, update_rule, delete_rule FROM information_schema.referential_constraints r
  JOIN information_schema.table_constraints t USING (constraint_name)
  WHERE t.table_name='ism_c';
SELECT check_clause FROM information_schema.check_constraints
  WHERE constraint_name LIKE 'ism\_c%' ORDER BY 1;
SELECT table_name FROM information_schema.views WHERE table_name='ism_v';
SELECT sequence_name, data_type FROM information_schema.sequences WHERE sequence_name='ism_s';
SELECT viewname FROM pg_views WHERE viewname='ism_v';
SELECT sequencename FROM pg_sequences WHERE sequencename='ism_s';
SELECT datname FROM pg_database WHERE datname=current_database();
DROP VIEW ism_v; DROP SEQUENCE ism_s; DROP TABLE ism_c, ism_p;
