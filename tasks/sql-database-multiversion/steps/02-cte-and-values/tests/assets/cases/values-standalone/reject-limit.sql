VALUES(5,'e');
-- lang_select.html: "A VALUES clause cannot be followed by ORDER BY or LIMIT."
VALUES(1),(2) LIMIT 1;
VALUES(6,'f');
SELECT * FROM (VALUES(7),(8)) ORDER BY 1 LIMIT 1;
