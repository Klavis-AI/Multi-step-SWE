-- lang_createindex.html: the index key is "a parenthesized list of table
-- column names and/or expressions"; "Each column name or expression can be
-- followed by one of the 'ASC' or 'DESC' keywords to indicate sort order";
-- "The COLLATE clause optionally following each column name or expression
-- defines a collating sequence used for text entries".
CREATE TABLE s(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, t TEXT);
INSERT INTO s VALUES(1,-5,2,'Beta'),(2,3,4,'alpha'),(3,-1,6,'Gamma');
CREATE INDEX s1 ON s(abs(a));
CREATE INDEX s2 ON s(a+b DESC);
CREATE INDEX s3 ON s(lower(t) COLLATE NOCASE ASC);
CREATE INDEX s4 ON s(substr(t,1,2));
CREATE INDEX s5 ON s(CASE WHEN a<0 THEN 0 ELSE 1 END, b);
CREATE INDEX s6 ON s(t || '!');
CREATE INDEX s7 ON s(length(t), id);
-- PROFILE.md 3.4 names CAST among the constructs an index expression may
-- be built from; lang_expr.html defines it and step 1 section 3 has it in
-- scope.
CREATE INDEX s8 ON s(CAST(a AS TEXT));
SELECT id FROM s WHERE abs(a)=5;
SELECT id, a+b FROM s ORDER BY a+b DESC;
SELECT id FROM s WHERE lower(t)='beta';
SELECT id FROM s WHERE substr(t,1,2)='al';
SELECT id FROM s WHERE (CASE WHEN a<0 THEN 0 ELSE 1 END)=0 ORDER BY id;
SELECT id FROM s WHERE t||'!'='Gamma!';
SELECT id, length(t) FROM s ORDER BY length(t), id;
SELECT id FROM s WHERE CAST(a AS TEXT)='3';
SELECT count(*) FROM s;
-- lang_createindex.html: "If the optional IF NOT EXISTS clause is present and
-- another index with the same name already exists, then this command becomes
-- a no-op."
CREATE INDEX IF NOT EXISTS s1 ON s(abs(a));
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='s';
