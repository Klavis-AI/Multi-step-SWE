SELECT to_regclass('pg_catalog.pg_publication') IS NOT NULL AS has_pg_publication,
       to_regclass('pg_catalog.pg_publication_rel') IS NOT NULL AS has_pg_publication_rel,
       to_regclass('pg_catalog.pg_publication_tables') IS NOT NULL AS has_pub_tables,
       to_regclass('pg_catalog.pg_subscription') IS NOT NULL AS has_pg_subscription,
       to_regclass('pg_catalog.pg_subscription_rel') IS NOT NULL AS has_sub_rel;
SELECT relkind FROM pg_class WHERE relname = 'pg_publication';
SELECT count(*) FROM pg_proc WHERE proname = 'pg_get_publication_tables';
