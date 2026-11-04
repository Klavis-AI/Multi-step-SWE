-- The equivalence of lang_select.html's "The VALUES clause" section, observed
-- from both sides.
SELECT count(*) FROM (VALUES(1,'x'));
SELECT count(*) FROM (SELECT 1,'x');
SELECT count(*) FROM (VALUES(1,'x'),(2,'y'),(3,'z'));
SELECT count(*) FROM (SELECT 1,'x' UNION ALL SELECT 2,'y' UNION ALL SELECT 3,'z');
SELECT * FROM (VALUES(1,'x'),(2,'y'),(3,'z') EXCEPT SELECT 2,'y') ORDER BY 1;
SELECT * FROM (SELECT 1,'x' UNION ALL SELECT 2,'y' UNION ALL SELECT 3,'z' EXCEPT SELECT 2,'y')
  ORDER BY 1;
