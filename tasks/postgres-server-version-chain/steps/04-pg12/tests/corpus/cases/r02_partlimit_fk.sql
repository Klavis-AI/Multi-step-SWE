CREATE TABLE q2_plf_ref(a int PRIMARY KEY);
CREATE TABLE q2_plf(a int, b text) PARTITION BY RANGE (a);
ALTER TABLE q2_plf ADD CONSTRAINT q2_plf_fk FOREIGN KEY (a) REFERENCES q2_plf_ref(a);
CREATE TABLE q2_plf_child(a int REFERENCES q2_plf_ref(a), b text) PARTITION BY RANGE (a);
CREATE TABLE q2_plf_back(a int REFERENCES q2_plf(a));
DROP TABLE IF EXISTS q2_plf_back;
DROP TABLE IF EXISTS q2_plf_child;
DROP TABLE IF EXISTS q2_plf;
DROP TABLE IF EXISTS q2_plf_ref;
