-- lang_select.html: "A VALUES clause can be the first element in a compound
-- SELECT that uses a WITH clause".
WITH c(x) AS (SELECT 7) VALUES(1) UNION ALL SELECT x FROM c ORDER BY 1;
WITH c(x) AS (VALUES(3),(4)) VALUES(1) UNION ALL SELECT x FROM c ORDER BY 1;
WITH c(x) AS (SELECT 5) SELECT * FROM (VALUES(1) UNION ALL SELECT x FROM c) ORDER BY 1;
