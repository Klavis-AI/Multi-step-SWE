CREATE TABLE q4_tab(a int) USING nosuchmethod;
CREATE TABLE q4_tac(a int) USING btree;
SET default_table_access_method = 'nosuchmethod';
SET default_table_access_method = 'heap';
SELECT count(*) FROM pg_class WHERE relname IN ('q4_tab','q4_tac');
