SELECT * FROM (SELECT 1 UNION ALL VALUES(2)) ORDER BY 1;
-- lang_select.html: "If the right-most element of a compound SELECT is a
-- VALUES clause, then no ORDER BY clause is allowed on that statement" and
-- "If the right-most simple SELECT is a VALUES clause then no LIMIT clause is
-- allowed."
SELECT 1 UNION ALL VALUES(2) ORDER BY 1;
SELECT 1 UNION ALL VALUES(2) LIMIT 1;
SELECT * FROM (VALUES(3) UNION ALL SELECT 4) ORDER BY 1 LIMIT 1;
