SELECT abs(-5), abs(5), abs(-2.5), abs(NULL) IS NULL, abs('abc');
SELECT round(2.4), round(2.6), round(-2.6), round(2.253, 2), round(0.7461, 2);
SELECT typeof(round(2.0)), round(2.0);
SELECT max(1, 2, 3), min(1, 2, 3), max('a', 'b'), min('a', 'b');
SELECT max(1, NULL, 3) IS NULL, min(1, NULL, 3) IS NULL;
