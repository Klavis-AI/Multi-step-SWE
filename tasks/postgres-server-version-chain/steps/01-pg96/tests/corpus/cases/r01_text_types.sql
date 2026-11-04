CREATE TABLE tx1(a char(5), b varchar(5), c text);
INSERT INTO tx1 VALUES ('ab','ab','ab');
INSERT INTO tx1 VALUES ('abcde ','abcde','abcde');
INSERT INTO tx1 VALUES ('abcdef','x','y');
SELECT a||'|', b||'|', c||'|', length(a), length(b) FROM tx1 ORDER BY c;
INSERT INTO tx1(b) VALUES ('abcdef');
SELECT 'ab '::char(5) = 'ab'::char(5), 'ab '::text = 'ab'::text;
DROP TABLE tx1;
