-- R14.4: a view over an aggregate returns the same rows as the aggregate written inline, keeping its GROUP BY grouping.
-- A view whose body aggregates keeps its own grouping when it is expanded:
-- the outer query sees one row per group, not the ungrouped rows.
CREATE TABLE sale(id INTEGER PRIMARY KEY, region TEXT, amt INTEGER);
INSERT INTO sale VALUES(1,'north',10),(2,'north',20),(3,'south',5),(4,'south',5),(5,'south',40);
CREATE VIEW by_region AS SELECT region, sum(amt) AS total, count(*) AS n FROM sale GROUP BY region;
SELECT region, total, n FROM by_region ORDER BY region;
SELECT region, sum(amt), count(*) FROM sale GROUP BY region ORDER BY region;
SELECT region, total FROM by_region WHERE total > 25 ORDER BY region;
