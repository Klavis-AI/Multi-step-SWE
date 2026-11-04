CREATE TABLE r10nn2(a int NOT NULL, b int);
SELECT constraint_name = 'r10nn2_a_not_null' AS named_for_column, constraint_type
  FROM information_schema.table_constraints WHERE table_name = 'r10nn2' ORDER BY 1, 2;
SELECT tc.constraint_name = 'r10nn2_a_not_null' AS named_for_column, cc.check_clause
  FROM information_schema.table_constraints tc
  JOIN information_schema.check_constraints cc USING (constraint_name)
  WHERE tc.table_name = 'r10nn2' ORDER BY 1, 2;
SELECT count(*) FROM information_schema.table_constraints WHERE table_name = 'r10nn2';
SELECT column_name, is_nullable FROM information_schema.columns WHERE table_name = 'r10nn2' ORDER BY ordinal_position;
DROP TABLE r10nn2;
