-- R07.14: a column omitted from the INSERT column list takes its declared
-- DEFAULT, a column with no DEFAULT takes NULL, and an explicit value
-- overrides both.
--
-- The NULL is reported through a substitute so the row never renders as an
-- empty line.
CREATE TABLE d(id INTEGER, n INTEGER DEFAULT 42, s TEXT DEFAULT 'unset', plain TEXT);
INSERT INTO d(id) VALUES(1);
INSERT INTO d(id,n) VALUES(2,7);
INSERT INTO d(id,s) VALUES(3,'given');
INSERT INTO d VALUES(4,0,'all','explicit');

SELECT id||':'||n||':'||s||':'||coalesce(plain,'<null>') FROM d ORDER BY id;
SELECT 'defaulted-n='||count(*) FROM d WHERE n=42;
SELECT 'defaulted-s='||count(*) FROM d WHERE s='unset';
SELECT 'null-plain='||count(*) FROM d WHERE plain IS NULL;
