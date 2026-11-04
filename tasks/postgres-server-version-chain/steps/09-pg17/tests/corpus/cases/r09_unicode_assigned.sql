SELECT unicode_assigned('abc'), unicode_assigned('');
SELECT unicode_assigned(U&'\+01D6C1');
SELECT count(*) FROM pg_proc WHERE proname IN ('unicode_assigned','unicode_version','icu_unicode_version');
SELECT unicode_assigned(NULL) IS NULL;
