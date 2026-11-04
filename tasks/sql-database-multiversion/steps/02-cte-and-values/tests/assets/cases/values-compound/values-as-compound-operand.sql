-- lang_select.html speaks of a compound SELECT "whose final element is a
-- VALUES clause" and of "a VALUES clause as the first element in a compound
-- SELECT", so a VALUES clause may stand on either side of a compound operator.
-- PROFILE.md section 2.2 keeps a multi-row VALUES clause left-most.
SELECT * FROM (SELECT 1 UNION ALL VALUES(2)) ORDER BY 1;
SELECT * FROM (VALUES(1) UNION ALL SELECT 2) ORDER BY 1;
SELECT * FROM (VALUES(1),(2) UNION ALL VALUES(3)) ORDER BY 1;
SELECT * FROM (VALUES(1),(2) UNION VALUES(2)) ORDER BY 1;
SELECT * FROM (VALUES(1),(2),(3) INTERSECT SELECT 2) ORDER BY 1;
SELECT * FROM (VALUES(1),(2),(3) EXCEPT VALUES(2)) ORDER BY 1;
