SELECT * FROM (VALUES(1,'a'),(2,'b')) ORDER BY 1;
-- Every row-value list of one VALUES clause has the same number of terms:
-- lang_select.html's compound rule ("all the constituent SELECTs must return
-- the same number of result columns") through the VALUES equivalence.
SELECT * FROM (VALUES(1,2),(3));
VALUES(1,2),(3);
SELECT count(*) FROM (VALUES(9),(8));
