-- partialindex.html's own Unique Partial Indexes example, minus its
-- "REFERENCES team" clause (foreign keys are out of scope, PROFILE.md
-- step 1 section 8): "The solution to enforcing one leader per team is to
-- create a unique index on team_id but restricted to those entries for which
-- is_team_leader is true".  PROFILE.md 4.2: a conflict target identifies such
-- an index by repeating its WHERE clause.
CREATE TABLE person(
  person_id INTEGER PRIMARY KEY,
  team_id INTEGER,
  is_team_leader BOOLEAN,
  name TEXT
);
CREATE UNIQUE INDEX team_leader ON person(team_id) WHERE is_team_leader;
INSERT INTO person VALUES(1,10,1,'ann'),(2,10,0,'bob'),(3,20,1,'cid');
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- a second leader for team 10 collides with the partial unique index, so the
-- existing leader row is updated instead of a row being inserted
INSERT INTO person VALUES(4,10,1,'dee')
  ON CONFLICT(team_id) WHERE is_team_leader DO UPDATE SET name=excluded.name;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
SELECT count(*) FROM person;
-- a leader for a team that has none collides with nothing and is inserted
INSERT INTO person VALUES(5,30,1,'eve')
  ON CONFLICT(team_id) WHERE is_team_leader DO UPDATE SET name=excluded.name;
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- the conflict target's own WHERE clause and the DO UPDATE's trailing WHERE
-- clause are two different clauses of the grammar PROFILE.md 4.1 writes out,
-- and they compose: the first identifies the partial unique index, the second
-- decides whether the update happens for that row (PROFILE.md 4.2, 4.4).
-- A trailing clause that is not true makes the upsert a no-op for the row
INSERT INTO person VALUES(6,30,1,'fay')
  ON CONFLICT(team_id) WHERE is_team_leader
  DO UPDATE SET name=excluded.name WHERE person.name <> 'eve';
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- and one that is true updates the row the partial index identified
INSERT INTO person VALUES(7,30,1,'gus')
  ON CONFLICT(team_id) WHERE is_team_leader
  DO UPDATE SET name=excluded.name WHERE person.name = 'eve';
SELECT person_id,team_id,is_team_leader,name FROM person ORDER BY person_id;
-- pragma.html, index_list: partial is 1 for a partial index (PROFILE.md 3.5)
PRAGMA index_list(person);
PRAGMA integrity_check;
