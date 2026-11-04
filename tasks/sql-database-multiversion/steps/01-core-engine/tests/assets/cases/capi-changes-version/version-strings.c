#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;

  printf("macro=%s\n", SQLITE_VERSION);
  printf("libversion=%s\n", sqlite3_libversion());
  printf("match=%d\n", strcmp(sqlite3_libversion(), SQLITE_VERSION) == 0);

  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
  if (sqlite3_prepare_v2(db, "SELECT sqlite_version()", -1, &st, 0) != SQLITE_OK) return 2;
  sqlite3_step(st);
  printf("sql_function=%s\n", sqlite3_column_text(st, 0));
  sqlite3_finalize(st);
  sqlite3_close(db);
  return 0;
}
