CREATE TABLE person(person_id INTEGER PRIMARY KEY, team_id, is_team_leader);
CREATE UNIQUE INDEX team_leader ON person(team_id) WHERE is_team_leader;
INSERT INTO person VALUES(1,10,1);
INSERT INTO person VALUES(2,10,0);
INSERT INTO person VALUES(3,10,1);
SELECT person_id, team_id, is_team_leader FROM person ORDER BY person_id;
