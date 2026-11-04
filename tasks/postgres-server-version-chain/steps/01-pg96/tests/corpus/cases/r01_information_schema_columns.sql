CREATE TABLE is1(a int primary key, b varchar(10), c numeric(8,3) DEFAULT 1.5,
                 d timestamptz, e text NOT NULL, f int[]);
SELECT column_name, ordinal_position, is_nullable, data_type, character_maximum_length,
       numeric_precision, numeric_scale, column_default
  FROM information_schema.columns WHERE table_name='is1' ORDER BY ordinal_position;
SELECT table_name, table_type FROM information_schema.tables WHERE table_name='is1';
SELECT constraint_type FROM information_schema.table_constraints
  WHERE table_name='is1' AND constraint_type <> 'CHECK' ORDER BY 1;
SELECT column_name FROM information_schema.key_column_usage WHERE table_name='is1' ORDER BY 1;
DROP TABLE is1;
