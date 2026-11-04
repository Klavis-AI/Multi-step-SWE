SELECT 2 + 3 * 4, (2 + 3) * 4, 6 / 3, 7 % 3;
SELECT 'a' || 'b' || 'c', 1 || 2;
SELECT 6 & 3, 6 | 3, 1 << 3, 16 >> 2, ~0;
SELECT -(-5), +'abc', typeof(+'abc');
SELECT 'x' + 1, 'x' * 2, '3x' + 1;
SELECT 1.0 / 2, typeof(1.0 / 2), 2.5 * 2;
SELECT (NULL + 1) IS NULL, (NULL * 0) IS NULL, (NULL || 'x') IS NULL;
