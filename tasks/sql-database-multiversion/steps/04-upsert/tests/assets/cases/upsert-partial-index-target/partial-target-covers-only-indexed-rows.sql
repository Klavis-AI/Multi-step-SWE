-- partialindex.html: "Only rows of the table for which the WHERE clause
-- evaluates to true are included in the index."  A conflict target that
-- identifies a partial unique index therefore triggers only for rows the
-- index covers (PROFILE.md 4.2, 4.3).
CREATE TABLE person(
  person_id INTEGER PRIMARY KEY,
  team_id INTEGER,
  is_team_leader BOOLEAN,
  name TEXT
);
CREATE UNIQUE INDEX team_leader ON person(team_id) WHERE is_team_leader;
INSERT INTO person VALUES(1,10,1,'ann'),(2,10,0,'bob');
-- a non-leader for team 10 is not in the index, so it is inserted even though
-- another row already has team_id 10
INSERT INTO person VALUES(3,10,0,'cid')
  ON CONFLICT(team_id) WHERE is_team_leader DO UPDATE SET name=excluded.name;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- DO NOTHING with the same target skips the colliding leader row and inserts
-- the non-colliding one, in a single statement
INSERT INTO person VALUES(4,10,1,'dee'),(5,40,1,'eve')
  ON CONFLICT(team_id) WHERE is_team_leader DO NOTHING;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- the index still enforces one leader per team outside any upsert
INSERT INTO person VALUES(6,40,0,'fay');
SELECT count(*) FROM person;
SELECT person_id FROM person WHERE is_team_leader AND team_id=10 ORDER BY person_id;
PRAGMA integrity_check;
