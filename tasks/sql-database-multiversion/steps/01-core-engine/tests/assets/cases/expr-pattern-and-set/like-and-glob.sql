SELECT 'abc' LIKE 'a%', 'abc' LIKE 'a_c', 'abc' LIKE 'A%', 'abc' LIKE 'b%';
SELECT 'abc' NOT LIKE 'a%', 'a' LIKE 'A';
SELECT '100%' LIKE '100!%' ESCAPE '!', '100x' LIKE '100!%' ESCAPE '!';
SELECT 'a_b' LIKE 'a!_b' ESCAPE '!', 'axb' LIKE 'a!_b' ESCAPE '!';
SELECT 'abc' GLOB 'a*', 'abc' GLOB 'a?c', 'abc' GLOB 'A*', 'abc' NOT GLOB 'z*';
SELECT like('a%','abc'), glob('a*','abc');
SELECT (NULL LIKE 'a%') IS NULL;
