/* TEST_INTERFACE.md 7: a statement PROFILE.md 4.7 says must be rejected fails
   at the library boundary too -- the call reports failure and the database is
   unchanged.  Only "did not return SQLITE_OK" is compared: the result code
   beyond that, and which call in a prepare/step sequence surfaces the failure,
   are not scored (TEST_INTERFACE.md 3.3, 6, 7). */
#include <stdio.h>
#include "sqlite3.h"

static sqlite3 *db;

/* Runs one statement and prints whether it was rejected.  sqlite3_exec()
   drives prepare and step for us, so a failure at either shows up here. */
static void rejected(const char *label, const char *sql) {
  char *msg = 0;
  int rc = sqlite3_exec(db, sql, 0, 0, &msg);
  printf("%s|%d\n", label, rc != SQLITE_OK);
  sqlite3_free(msg);
}

static int count(const char *sql) {
  sqlite3_stmt *st = 0;
  int n = -1;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return -1;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

int main(void) {
  if (sqlite3_open("r.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db,
        "CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, c TEXT NOT NULL);"
        "CREATE TABLE p(pid INTEGER PRIMARY KEY, team INTEGER, lead INTEGER);"
        "CREATE UNIQUE INDEX pl ON p(team) WHERE lead;"
        "INSERT INTO t VALUES(1,'x','one'),(2,'y','two');"
        "INSERT INTO p VALUES(1,10,1);",
        0, 0, 0) != SQLITE_OK) return 2;

  /* A well-formed upsert before the rejected ones. */
  rejected("ok-update",
    "INSERT INTO t VALUES(1,'p','ONE') ON CONFLICT(k) DO UPDATE SET c=excluded.c;");
  rejected("no-target",
    "INSERT INTO t VALUES(1,'q','X') ON CONFLICT DO UPDATE SET c=excluded.c;");
  rejected("default-values",
    "INSERT INTO t DEFAULT VALUES ON CONFLICT DO NOTHING;");
  rejected("target-not-unique",
    "INSERT INTO t VALUES(3,'z','THREE') ON CONFLICT(c) DO UPDATE SET c=excluded.c;");
  rejected("partial-target-without-where",
    "INSERT INTO p VALUES(2,10,1) ON CONFLICT(team) DO UPDATE SET lead=excluded.lead;");
  rejected("untargeted-constraint",
    "INSERT INTO t VALUES(4,'x','FOUR') ON CONFLICT(k) DO UPDATE SET c=excluded.c;");
  rejected("do-update-breaks-not-null",
    "INSERT INTO t VALUES(1,'r','X') ON CONFLICT(k) DO UPDATE SET c=NULL;");
  /* And a well-formed one afterwards, to show the connection still works. */
  rejected("ok-nothing",
    "INSERT INTO t VALUES(2,'s','TWO') ON CONFLICT DO NOTHING;");

  printf("rows_t|%d\n", count("SELECT count(*) FROM t"));
  printf("rows_p|%d\n", count("SELECT count(*) FROM p"));
  printf("c_of_1|%d\n", count("SELECT count(*) FROM t WHERE k=1 AND c='ONE'"));
  printf("c_of_2|%d\n", count("SELECT count(*) FROM t WHERE k=2 AND c='two'"));
  sqlite3_close(db);
  return 0;
}
