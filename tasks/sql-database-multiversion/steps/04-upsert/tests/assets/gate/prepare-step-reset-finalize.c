#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  const char *tail = 0;
  const char *sql = "SELECT a FROM t ORDER BY a; SELECT 1;";
  int rc, n;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY);"
                   "INSERT INTO t VALUES(1),(2),(3);", 0, 0, 0);

  printf("prepare=%d\n", sqlite3_prepare_v2(db, sql, -1, &st, &tail) == SQLITE_OK);
  printf("tail=%s\n", tail);

  n = 0;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    n++;
    printf("step row %d\n", sqlite3_column_int(st, 0));
  }
  printf("rows=%d done=%d\n", n, rc == SQLITE_DONE);

  printf("reset=%d\n", sqlite3_reset(st) == SQLITE_OK);
  n = 0;
  while (sqlite3_step(st) == SQLITE_ROW) n++;
  printf("rows_after_reset=%d\n", n);
  printf("finalize=%d\n", sqlite3_finalize(st) == SQLITE_OK);

  /* a statement that returns no data */
  if (sqlite3_prepare_v2(db, "INSERT INTO t VALUES(4)", -1, &st, 0) != SQLITE_OK) return 2;
  printf("insert_columns=%d\n", sqlite3_column_count(st));
  printf("insert_step_done=%d\n", sqlite3_step(st) == SQLITE_DONE);
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
