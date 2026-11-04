SELECT typname, typtype, typlen FROM pg_type
  WHERE typname IN ('xid','xid8','pg_snapshot','txid_snapshot') ORDER BY typname;
SELECT 'xid8'::regtype::text, 'pg_snapshot'::regtype::text;
SELECT count(*) FROM pg_proc WHERE proname LIKE 'pg_current_xact_id%';
SELECT proname FROM pg_proc WHERE proname LIKE 'pg_snapshot%' ORDER BY proname, pronargs;
