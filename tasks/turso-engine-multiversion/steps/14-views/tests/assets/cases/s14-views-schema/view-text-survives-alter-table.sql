-- R14.9: ALTER TABLE ADD COLUMN on a table a view reads from leaves the view's stored text unchanged and the view still resolves.
-- Altering a table a view reads from does not rewrite the view's stored text,
-- and the view keeps resolving against the altered table: the added column is
-- simply not one the view projects.
CREATE TABLE base(id INTEGER PRIMARY KEY, tag TEXT);
INSERT INTO base VALUES(1,'x');
CREATE VIEW tagged AS SELECT id, tag FROM base;
ALTER TABLE base ADD COLUMN extra INTEGER;
INSERT INTO base VALUES(2,'y',42);
SELECT sql FROM sqlite_schema WHERE name='tagged';
SELECT id, tag FROM tagged ORDER BY id;
SELECT id, tag, extra FROM base ORDER BY id;
