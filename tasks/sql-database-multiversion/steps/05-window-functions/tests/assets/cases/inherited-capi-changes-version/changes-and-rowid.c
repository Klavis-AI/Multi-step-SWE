#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  printf("initial_total=%d initial_rowid=%lld\n",
         sqlite3_total_changes(db), (long long)sqlite3_last_insert_rowid(db));

  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);", 0, 0, 0);
  printf("after_ddl_total=%d\n", sqlite3_total_changes(db));

  sqlite3_exec(db, "INSERT INTO t VALUES(10,'p');", 0, 0, 0);
  printf("ins1 changes=%d total=%d rowid=%lld\n",
         sqlite3_changes(db), sqlite3_total_changes(db),
         (long long)sqlite3_last_insert_rowid(db));

  if (sqlite3_prepare_v2(db, "INSERT INTO t(b) VALUES(?)", -1, &st, 0) != SQLITE_OK) return 2;
  sqlite3_bind_text(st, 1, "q", -1, SQLITE_STATIC);
  sqlite3_step(st);
  sqlite3_finalize(st);
  printf("ins2 changes=%d total=%d rowid=%lld\n",
         sqlite3_changes(db), sqlite3_total_changes(db),
         (long long)sqlite3_last_insert_rowid(db));

  sqlite3_exec(db, "UPDATE t SET b = 'z';", 0, 0, 0);
  printf("upd changes=%d total=%d rowid=%lld\n",
         sqlite3_changes(db), sqlite3_total_changes(db),
         (long long)sqlite3_last_insert_rowid(db));

  sqlite3_exec(db, "DELETE FROM t WHERE a = 10;", 0, 0, 0);
  printf("del changes=%d total=%d\n", sqlite3_changes(db), sqlite3_total_changes(db));

  /* a failed INSERT does not move last_insert_rowid */
  sqlite3_exec(db, "INSERT INTO t VALUES(20,'r');", 0, 0, 0);
  sqlite3_exec(db, "INSERT OR IGNORE INTO t VALUES(20,'s');", 0, 0, 0);
  printf("after_ignored rowid=%lld\n", (long long)sqlite3_last_insert_rowid(db));

  sqlite3_close(db);
  return 0;
}
