CREATE TEMP TABLE x07_50s AS SELECT has_schema_privilege('public','public','CREATE') AS orig;
GRANT CREATE ON SCHEMA public TO PUBLIC;
SELECT has_schema_privilege('public','public','CREATE');
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
SELECT has_schema_privilege('public','public','CREATE');
DO $$ DECLARE o bool; BEGIN SELECT orig INTO o FROM x07_50s;
  IF o THEN EXECUTE 'GRANT CREATE ON SCHEMA public TO PUBLIC'; END IF; END $$;
SELECT has_schema_privilege('public','public','CREATE') AS restored;
DROP TABLE x07_50s;
