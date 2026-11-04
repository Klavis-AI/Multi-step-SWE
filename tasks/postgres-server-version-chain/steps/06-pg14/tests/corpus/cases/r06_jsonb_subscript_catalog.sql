SELECT typname, typsubscript::text FROM pg_type
  WHERE typname IN ('jsonb','json','_int4','text') ORDER BY typname;
SELECT count(*) FROM pg_attribute WHERE attrelid='pg_type'::regclass AND attname='typsubscript';
SELECT proname FROM pg_proc WHERE proname IN ('jsonb_subscript_handler','array_subscript_handler')
  ORDER BY proname;
