-- lang_altertable.html: "when adding a CHECK constraint, the CHECK constraint
-- is not tested against preexisting rows of the table. This can result in a
-- table that contains data that is in violation of the CHECK constraint."
-- PROFILE.md §1: ADD COLUMN is in scope; existing rows read the column's DEFAULT.
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(5);
ALTER TABLE t ADD COLUMN b INTEGER DEFAULT 0 CHECK(b > 10);
SELECT a, b FROM t;
INSERT INTO t(a, b) VALUES(7, 20);
SELECT a, b FROM t ORDER BY a;
