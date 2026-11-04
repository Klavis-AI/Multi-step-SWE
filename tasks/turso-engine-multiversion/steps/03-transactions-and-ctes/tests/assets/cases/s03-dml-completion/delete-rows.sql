-- R03.13: DELETE ... WHERE removes exactly the row its predicate selects; the
-- survivors keep their values and their rowid order, and count() agrees.
CREATE TABLE u(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO u VALUES(1,'a'),(2,'b'),(3,'c');
DELETE FROM u WHERE id=3;
SELECT id, name FROM u ORDER BY id;
SELECT count(*) FROM u;
