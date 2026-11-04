SELECT 'abc' LIKE 'a%', 'abc' LIKE '_b_', 'abc' LIKE 'A%', 'abc' ILIKE 'A%';
SELECT 'a%c' LIKE 'a\\%c', 'a%c' LIKE 'a%c';
SELECT 'abc' NOT LIKE 'x%', 'abc' SIMILAR TO 'a%';
SELECT 'a_c' LIKE 'a$_c' ESCAPE '$';
SELECT NULL LIKE 'a%', 'a' LIKE NULL;
