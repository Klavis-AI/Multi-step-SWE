SELECT typname, typtype, typcategory FROM pg_type
  WHERE typname IN ('int4range','int4multirange','nummultirange','datemultirange')
  ORDER BY typname;
SELECT rngtypid::regtype::text AS rngtype, rngmultitypid::regtype::text AS multitype
  FROM pg_range WHERE rngtypid IN ('int4range'::regtype,'numrange'::regtype)
  ORDER BY rngtype;
SELECT count(*) FROM pg_type WHERE typtype = 'm' AND typname LIKE '%multirange';
