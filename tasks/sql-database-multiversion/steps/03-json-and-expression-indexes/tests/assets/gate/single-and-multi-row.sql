-- lang_select.html: "The phrase VALUES(expr-list) means the same thing as
-- SELECT expr-list", and N row-value lists mean N SELECTs joined by UNION ALL,
-- so the rows appear in the order written (PROFILE.md section 2.2).
VALUES(1);
VALUES(1,2);
VALUES(1,'a',NULL,2.5);
VALUES(1,'a'),(2,'b'),(3,'c');
VALUES(1+1, 'a'||'b', 3*4);
