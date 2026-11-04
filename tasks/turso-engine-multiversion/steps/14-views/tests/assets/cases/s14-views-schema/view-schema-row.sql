-- R14.8: a view's `sqlite_schema` row carries type 'view', its own name in `tbl_name`, root page zero, and the engine's re-print of the CREATE VIEW text.
-- A view's schema row has type 'view', names itself in both name and
-- tbl_name, and carries root page zero because a view allocates no B-tree.
-- The stored text is the engine's rendering of the CREATE VIEW statement.
CREATE TABLE base(id INTEGER PRIMARY KEY, tag TEXT);
INSERT INTO base VALUES(1,'x');
CREATE VIEW tagged AS SELECT id, tag FROM base WHERE tag IS NOT NULL;
SELECT type, name, tbl_name, rootpage FROM sqlite_schema ORDER BY name;
SELECT sql FROM sqlite_schema WHERE name='tagged';
SELECT count(*) FROM sqlite_schema WHERE type='view' AND rootpage <> 0;
