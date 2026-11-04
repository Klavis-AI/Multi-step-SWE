-- lang_select.html: "When comparing values as a result of a USING clause, the
-- normal rules for handling affinities, collation sequences and NULL values
-- in comparisons apply." INTEGER affinity on the left converts TEXT '1', so
-- the join matches; "the column from the right-hand dataset is omitted".
CREATE TABLE l(a INTEGER, x TEXT);
CREATE TABLE r(a TEXT, y TEXT);
INSERT INTO l VALUES(1,'L');
INSERT INTO r VALUES('1','R');
SELECT * FROM l JOIN r USING(a);
