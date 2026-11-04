VALUES(1,'a'),(2,'b');
-- lang_select.html: "A VALUES clause cannot be followed by ORDER BY or LIMIT."
VALUES(1),(2) ORDER BY 1;
SELECT * FROM (VALUES(3),(4)) ORDER BY 1;
