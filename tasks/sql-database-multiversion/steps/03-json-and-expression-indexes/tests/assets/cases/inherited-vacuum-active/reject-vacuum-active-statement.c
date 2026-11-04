/* lang_vacuum.html: VACUUM fails if there are one or more active SQL
   statements. A statement becomes active on sqlite3_step, not merely on
   prepare; the already-scored reject-vacuum-in-transaction.sh covers the
   open-transaction half of the same sentence. */
#include <stdio.h>
#include "sqlite3.h"

static int count_cb(void *arg, int ncol, char **vals, char **names) {
  (void)arg; (void)ncol; (void)names;
  printf("count=%s\n", vals[0]);
  return 0;
}

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db,
      "CREATE TABLE t(a INTEGER PRIMARY KEY); INSERT INTO t VALUES(1);",
      0, 0, 0);

  if (sqlite3_prepare_v2(db, "SELECT a FROM t", -1, &st, 0) != SQLITE_OK) return 2;
  if (sqlite3_step(st) != SQLITE_ROW) return 3;

  rc = sqlite3_exec(db, "VACUUM;", 0, 0, 0);
  printf("vacuum_while_active_ok=%d\n", rc == SQLITE_OK);
  printf("held_row=%d\n", sqlite3_column_int(st, 0));
  printf("finalize=%d\n", sqlite3_finalize(st) == SQLITE_OK);

  rc = sqlite3_exec(db, "VACUUM;", 0, 0, 0);
  printf("vacuum_after_finalize_ok=%d\n", rc == SQLITE_OK);
  sqlite3_exec(db, "SELECT count(*) FROM t;", count_cb, 0, 0);
  sqlite3_close(db);
  return 0;
}
