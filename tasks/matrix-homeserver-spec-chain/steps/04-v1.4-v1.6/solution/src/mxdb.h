/* SQLite storage: one file, one connection per thread. */
#ifndef MXDB_H
#define MXDB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sqlite3.h>

/* Opens (creating if needed) the database at `path` and applies the schema.
 * `path` is the plain filesystem path, already stripped of a "file:" prefix. */
bool db_open(const char *path);
void db_close(void);
sqlite3 *db(void);                 /* this thread's handle */

/* Small helpers.  All of them log and return false/NULL on error. */
bool db_exec(const char *sql);
bool db_execf(const char *fmt, ...);
/* A prepared-statement helper: bind with ?1.. and step once.
 * `types` is a string of 's' (const char*), 'i' (int64_t), 'b' (blob: const
 * void*, then size_t), 'n' (NULL). */
bool db_run(const char *sql, const char *types, ...);
/* Returns the first column of the first row as a freshly allocated string,
 * or NULL when there is no row. */
char *db_str(const char *sql, const char *types, ...);
bool db_int(const char *sql, int64_t *out, const char *types, ...);
int64_t db_last_insert_rowid(void);

bool db_begin(void);
bool db_commit(void);
bool db_rollback(void);

#endif
