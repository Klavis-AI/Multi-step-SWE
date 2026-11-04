SELECT ROW(1,'a'), (1,'a');
SELECT (1,2) = (1,2), (1,2) < (1,3), (1,NULL) IS NULL;
SELECT (ROW(1,2)).f1;
SELECT * FROM (VALUES (ROW(1,2))) v(r);
