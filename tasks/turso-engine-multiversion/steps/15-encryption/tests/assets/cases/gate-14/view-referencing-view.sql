-- R14.5: a view defined over another view resolves transitively through three levels, and the views are counted in `sqlite_schema`.
-- A view may be defined over another view; resolution is transitive and the
-- result is the fully expanded query. Three levels are used so that a
-- single-level-only expansion is caught.
CREATE TABLE n(v INTEGER);
INSERT INTO n VALUES(1),(2),(3),(4),(5),(6);
CREATE VIEW evens AS SELECT v FROM n WHERE v % 2 = 0;
CREATE VIEW big_evens AS SELECT v FROM evens WHERE v > 2;
CREATE VIEW biggest_even AS SELECT max(v) AS v FROM big_evens;
SELECT v FROM evens ORDER BY v;
SELECT v FROM big_evens ORDER BY v;
SELECT v FROM biggest_even;
SELECT count(*) FROM sqlite_schema WHERE type='view';
