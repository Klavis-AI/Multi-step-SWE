#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  const char *dt;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t1(c1 VARIANT, c2 INTEGER);"
                   "INSERT INTO t1 VALUES('x', 1);", 0, 0, 0);

  if (sqlite3_prepare_v2(db, "SELECT c1 + 1 AS expr, c1 AS plain, c2 AS num FROM t1",
                         -1, &st, 0) != SQLITE_OK) return 2;
  printf("count=%d\n", sqlite3_column_count(st));
  printf("names=%s,%s,%s\n", sqlite3_column_name(st, 0), sqlite3_column_name(st, 1),
         sqlite3_column_name(st, 2));
  dt = sqlite3_column_decltype(st, 0);
  printf("decl0_null=%d\n", dt == 0);
  printf("decl1=%s decl2=%s\n", sqlite3_column_decltype(st, 1),
         sqlite3_column_decltype(st, 2));
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
