CREATE TABLE q4_gcs(a int, b int GENERATED ALWAYS AS (a * 2) STORED, c text DEFAULT 'x');
SELECT column_name, is_generated, generation_expression, column_default, is_identity
  FROM information_schema.columns WHERE table_name = 'q4_gcs' ORDER BY ordinal_position;
DROP TABLE q4_gcs;
