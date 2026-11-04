/* sqlite3_open, sqlite3_exec with and without a callback, sqlite3_close. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static int cb(void *arg, int ncol, char **vals, char **names) {
  int i; int *rows = (int *)arg;
  (*rows)++;
  printf("row ncol=%d", ncol);
  for (i = 0; i < ncol; i++) {
    printf(" %s=%s", names[i], vals[i] ? vals[i] : "(null)");
  }
  printf("\n");
  return 0;
}

int main(void) {
  sqlite3 *db = 0;
  char *msg = 0;
  int rows = 0;
  int rc;

  rc = sqlite3_open("main.db", &db);
  printf("open=%d\n", rc == SQLITE_OK);

  rc = sqlite3_exec(db,
      "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);"
      "INSERT INTO t VALUES(1,'p');"
      "INSERT INTO t VALUES(2,NULL);", 0, 0, &msg);
  printf("ddl=%d msg_null=%d\n", rc == SQLITE_OK, msg == 0);

  rc = sqlite3_exec(db, "SELECT a AS ca, b AS cb FROM t ORDER BY a;", cb, &rows, 0);
  printf("select=%d rows=%d\n", rc == SQLITE_OK, rows);

  rc = sqlite3_exec(db, "", 0, 0, 0);
  printf("empty=%d\n", rc == SQLITE_OK);

  printf("close=%d\n", sqlite3_close(db) == SQLITE_OK);
  return 0;
}
