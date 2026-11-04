#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int ty;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);"
                   "INSERT INTO t VALUES(1,'x'),(2,'y');", 0, 0, 0);

  /* INTEGER requested as TEXT is the ASCII rendering of the integer */
  if (sqlite3_prepare_v2(db, "SELECT 12345", -1, &st, 0) != SQLITE_OK) return 2;
  sqlite3_step(st);
  printf("int_as_text=%s int_as_double=%.1f\n",
         sqlite3_column_text(st, 0), sqlite3_column_double(st, 0));
  sqlite3_finalize(st);

  /* an empty string is still a non-NULL, zero-terminated TEXT value */
  if (sqlite3_prepare_v2(db, "SELECT ''", -1, &st, 0) != SQLITE_OK) return 3;
  sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);   /* read before any other accessor */
  printf("empty_type=%d empty_bytes=%d empty_nonnull=%d\n",
         ty, sqlite3_column_bytes(st, 0),
         sqlite3_column_text(st, 0) != 0);
  sqlite3_finalize(st);

  /* sqlite3_data_count over the life of a statement */
  if (sqlite3_prepare_v2(db, "SELECT a, b FROM t ORDER BY a", -1, &st, 0) != SQLITE_OK) return 4;
  printf("before columns=%d data=%d\n", sqlite3_column_count(st), sqlite3_data_count(st));
  rc = sqlite3_step(st);
  printf("row1 rc_row=%d columns=%d data=%d\n",
         rc == SQLITE_ROW, sqlite3_column_count(st), sqlite3_data_count(st));
  sqlite3_step(st);
  rc = sqlite3_step(st);
  printf("after rc_done=%d columns=%d data=%d\n",
         rc == SQLITE_DONE, sqlite3_column_count(st), sqlite3_data_count(st));
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
