-- PROFILE.md section 2.2, "Row width", second clause: where a VALUES clause is
-- an operand of a compound operator its width must match the other operands' —
-- lang_select.html's "all the constituent SELECTs must return the same number
-- of result columns" applied through the VALUES equivalence. Both directions
-- are rejected. The multi-row VALUES clause stays left-most (section 2.5).
SELECT * FROM (VALUES(1,'a') UNION ALL SELECT 2,'b') ORDER BY 1;
VALUES(1,'a'),(2,'b') UNION ALL SELECT 3;
SELECT 1 UNION ALL VALUES(2,'b');
SELECT * FROM (VALUES(9,'z') UNION ALL SELECT 8,'y') ORDER BY 1;
