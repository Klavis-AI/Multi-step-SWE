-- PROFILE.md 4.2: a conflict target "specifies a specific uniqueness
-- constraint", so naming the indexed column of a partial unique index without
-- the WHERE clause that identifies it names no constraint and the statement is
-- rejected, leaving the table exactly as it was.
CREATE TABLE person(
  person_id INTEGER PRIMARY KEY,
  team_id INTEGER,
  is_team_leader BOOLEAN,
  name TEXT
);
CREATE UNIQUE INDEX team_leader ON person(team_id) WHERE is_team_leader;
INSERT INTO person VALUES(1,10,1,'ann'),(2,10,0,'bob');
-- the target that does identify the index is accepted
INSERT INTO person VALUES(3,10,1,'cid')
  ON CONFLICT(team_id) WHERE is_team_leader DO UPDATE SET name=excluded.name;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- the same target without the WHERE clause is rejected
INSERT INTO person VALUES(4,10,1,'dee')
  ON CONFLICT(team_id) DO UPDATE SET name=excluded.name;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
SELECT count(*) FROM person;
-- and so is a DO NOTHING carrying the same unmatched target
INSERT INTO person VALUES(5,10,1,'eve') ON CONFLICT(team_id) DO NOTHING;
SELECT count(*) FROM person;
PRAGMA integrity_check;
