-- R14.2: selecting from a view returns exactly the rows its defining SELECT returns written inline, under the outer query's own WHERE, ORDER BY and LIMIT.
-- A view is expanded at plan time, so selecting from it must return exactly
-- what its defining SELECT returns written inline -- including when the
-- outer query adds its own WHERE, ORDER BY and LIMIT on top of the view.
CREATE TABLE emp(id INTEGER PRIMARY KEY, dept TEXT, sal INTEGER);
INSERT INTO emp VALUES(1,'eng',100),(2,'eng',250),(3,'ops',50),(4,'ops',300);
CREATE VIEW well_paid AS SELECT id, dept, sal FROM emp WHERE sal >= 100;
SELECT id, dept, sal FROM well_paid ORDER BY id;
SELECT id, dept, sal FROM emp WHERE sal >= 100 ORDER BY id;
SELECT dept, sal FROM well_paid WHERE dept='ops' ORDER BY sal DESC LIMIT 1;
SELECT count(*) FROM well_paid;
