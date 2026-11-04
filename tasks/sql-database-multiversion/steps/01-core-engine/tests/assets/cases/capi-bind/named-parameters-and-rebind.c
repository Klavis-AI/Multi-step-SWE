#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int i;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(a, b, c, d);", 0, 0, 0);

  if (sqlite3_prepare_v2(db,
        "INSERT INTO t(a, b, c, d) VALUES(:one, @two, $three, ?9)", -1, &st, 0) != SQLITE_OK)
    return 2;
  printf("one=%d two=%d three=%d nine=%d missing=%d\n",
         sqlite3_bind_parameter_index(st, ":one"),
         sqlite3_bind_parameter_index(st, "@two"),
         sqlite3_bind_parameter_index(st, "$three"),
         sqlite3_bind_parameter_index(st, "?9"),
         sqlite3_bind_parameter_index(st, ":nope"));
  sqlite3_bind_text(st, sqlite3_bind_parameter_index(st, ":one"), "A", -1, SQLITE_STATIC);
  sqlite3_bind_text(st, sqlite3_bind_parameter_index(st, "@two"), "B", -1, SQLITE_STATIC);
  sqlite3_bind_text(st, sqlite3_bind_parameter_index(st, "$three"), "C", -1, SQLITE_STATIC);
  sqlite3_bind_int(st, 9, 99);
  printf("insert=%d\n", sqlite3_step(st) == SQLITE_DONE);
  sqlite3_finalize(st);

  /* bindings survive a reset, and can be replaced one at a time */
  if (sqlite3_prepare_v2(db, "INSERT INTO t(a, b, c, d) VALUES(?1, ?2, ?2, ?1)", -1, &st, 0)
      != SQLITE_OK) return 3;
  sqlite3_bind_int(st, 1, 10);
  sqlite3_bind_int(st, 2, 20);
  for (i = 0; i < 3; i++) {
    sqlite3_step(st);
    sqlite3_reset(st);
    sqlite3_bind_int(st, 2, 20 + i + 1);
  }
  sqlite3_finalize(st);

  if (sqlite3_prepare_v2(db, "SELECT a, b, c, d FROM t ORDER BY rowid", -1, &st, 0) != SQLITE_OK)
    return 4;
  while (sqlite3_step(st) == SQLITE_ROW) {
    printf("%s|%s|%s|%s\n", sqlite3_column_text(st, 0), sqlite3_column_text(st, 1),
           sqlite3_column_text(st, 2), sqlite3_column_text(st, 3));
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  return 0;
}
