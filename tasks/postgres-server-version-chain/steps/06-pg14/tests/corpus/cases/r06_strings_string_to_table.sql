SELECT s FROM string_to_table('a,b,c', ',') s ORDER BY s;
SELECT count(*) FROM string_to_table('a,b,,c', ',');
SELECT s IS NULL AS isnull, count(*) FROM string_to_table('a,b,,c', ',', 'b') s
  GROUP BY 1 ORDER BY 1;
SELECT s FROM string_to_table('abc', NULL) s ORDER BY s;
SELECT count(*) FROM string_to_table('', ',');
