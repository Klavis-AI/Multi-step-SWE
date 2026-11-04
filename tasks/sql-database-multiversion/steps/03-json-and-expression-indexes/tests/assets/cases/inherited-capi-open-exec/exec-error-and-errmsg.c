/* A failing sqlite3_exec reports an error, allocates a message that
   sqlite3_free releases, and leaves the database unchanged. */
#include <stdio.h>
#include "sqlite3.h"

static int count_cb(void *arg, int ncol, char **vals, char **names) {
  (void)ncol; (void)names;
  printf("count=%s\n", vals[0]);
  return 0;
}

int main(void) {
  sqlite3 *db = 0;
  char *msg = 0;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY); INSERT INTO t VALUES(1);", 0, 0, 0);

  rc = sqlite3_exec(db, "THIS IS NOT SQL;", 0, 0, &msg);
  printf("syntax_rc_ok=%d msg_present=%d\n", rc == SQLITE_OK, msg != 0);
  sqlite3_free(msg);
  msg = 0;

  rc = sqlite3_exec(db, "INSERT INTO t VALUES(2); INSERT INTO t VALUES(1); INSERT INTO t VALUES(3);",
                    0, 0, &msg);
  printf("constraint_rc_ok=%d msg_present=%d\n", rc == SQLITE_OK, msg != 0);
  sqlite3_free(msg);

  sqlite3_exec(db, "SELECT count(*) FROM t;", count_cb, 0, 0);
  sqlite3_close(db);
  return 0;
}
