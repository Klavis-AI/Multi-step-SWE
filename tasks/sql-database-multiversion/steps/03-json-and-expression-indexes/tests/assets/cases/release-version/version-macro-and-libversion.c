/* TEST_INTERFACE.md section 3.3: SQLITE_VERSION and sqlite3_libversion() both
   report the release this step targets, and they agree with sqlite_version(). */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;

  printf("macro=%s\n", SQLITE_VERSION);
  printf("macro_is_390=%d\n", strcmp(SQLITE_VERSION, "3.9.0") == 0);
  printf("libversion=%s\n", sqlite3_libversion());
  printf("libversion_matches_macro=%d\n", strcmp(sqlite3_libversion(), SQLITE_VERSION) == 0);

  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
  if (sqlite3_prepare_v2(db, "SELECT sqlite_version()", -1, &st, 0) != SQLITE_OK) return 2;
  if (sqlite3_step(st) != SQLITE_ROW) return 3;
  printf("sql_matches_macro=%d\n",
         strcmp((const char *)sqlite3_column_text(st, 0), SQLITE_VERSION) == 0);
  sqlite3_finalize(st);
  sqlite3_close(db);
  return 0;
}
