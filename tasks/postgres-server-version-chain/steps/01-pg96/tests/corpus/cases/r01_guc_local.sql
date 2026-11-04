BEGIN;
SET LOCAL work_mem = '3MB';
SHOW work_mem;
COMMIT;
SELECT current_setting('work_mem') IS NOT NULL;
SELECT set_config('work_mem','4MB',false);
SHOW work_mem;
RESET ALL;
