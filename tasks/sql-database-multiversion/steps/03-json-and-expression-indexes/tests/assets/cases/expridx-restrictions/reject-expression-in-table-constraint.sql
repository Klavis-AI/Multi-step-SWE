-- expridx.html, "Restrictions": "Expressions may only be used in CREATE INDEX
-- statements, not within UNIQUE or PRIMARY KEY constraints within the CREATE
-- TABLE statement."
-- The permitted spelling first: a UNIQUE index on an expression, created by a
-- CREATE INDEX statement, is fine.
CREATE TABLE ok(a INTEGER, b INTEGER);
CREATE UNIQUE INDEX oku ON ok(a + b);
INSERT INTO ok VALUES(1,2),(4,5);
SELECT a FROM ok WHERE a + b = 3;
-- the prohibited spellings
CREATE TABLE bad1(a, b, UNIQUE(a+b));
CREATE TABLE bad2(a, b, PRIMARY KEY(a+b));
CREATE TABLE bad3(a, b, UNIQUE(abs(a)));
CREATE TABLE bad4(a, b, PRIMARY KEY(lower(b)));
-- neither table exists and the permitted one still works
SELECT count(*) FROM sqlite_master WHERE type='table' AND name LIKE 'bad%';
SELECT count(*) FROM ok;
INSERT INTO ok VALUES(0,3);
SELECT count(*) FROM ok;
