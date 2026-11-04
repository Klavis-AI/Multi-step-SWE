/* initdb.c - create a database cluster data directory.
 * Flags and the resulting directory layout follow the PostgreSQL
 * documentation for initdb and "Database File Layout". */
#include "pgvc.h"
extern const char *PGVC_VERSION_TEXT;
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static void mkd(const char *base, const char *sub)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", base, sub);
    mkdir(p, 0700);
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *user = "postgres", *enc = "UTF8", *auth = "trust";
    const char *authlocal = NULL, *authhost = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            printf("initdb (PostgreSQL) %s\n", PGVC_VERSION_TEXT);
            return 0;
        }
        if (!strcmp(argv[i], "-D") || !strcmp(argv[i], "--pgdata")) { dir = argv[++i]; continue; }
        if (!strncmp(argv[i], "--pgdata=", 9)) { dir = argv[i] + 9; continue; }
        if (!strncmp(argv[i], "-D", 2) && argv[i][2]) { dir = argv[i] + 2; continue; }
        if (!strcmp(argv[i], "-U") || !strcmp(argv[i], "--username")) { user = argv[++i]; continue; }
        if (!strncmp(argv[i], "--username=", 11)) { user = argv[i] + 11; continue; }
        if (!strcmp(argv[i], "-E") || !strcmp(argv[i], "--encoding")) { enc = argv[++i]; continue; }
        if (!strncmp(argv[i], "--encoding=", 11)) { enc = argv[i] + 11; continue; }
        if (!strcmp(argv[i], "-A") || !strcmp(argv[i], "--auth")) { auth = argv[++i]; continue; }
        if (!strncmp(argv[i], "--auth=", 7)) { auth = argv[i] + 7; continue; }
        if (!strncmp(argv[i], "--auth-local=", 13)) { authlocal = argv[i] + 13; continue; }
        if (!strncmp(argv[i], "--auth-host=", 12)) { authhost = argv[i] + 12; continue; }
        if (!strcmp(argv[i], "--no-locale") || !strcmp(argv[i], "-N") ||
            !strcmp(argv[i], "--no-sync") || !strcmp(argv[i], "-n") ||
            !strcmp(argv[i], "--no-clean") || !strcmp(argv[i], "-d") ||
            !strcmp(argv[i], "--debug")) continue;
        if (!strncmp(argv[i], "--locale", 8) || !strncmp(argv[i], "--lc-", 5) ||
            !strncmp(argv[i], "--wal-segsize", 13) || !strncmp(argv[i], "--waldir", 8) ||
            !strncmp(argv[i], "--set", 5) || !strncmp(argv[i], "--locale-provider", 17) ||
            !strncmp(argv[i], "--icu-locale", 12)) { if (!strchr(argv[i], '=')) i++; continue; }
        if (argv[i][0] != '-' && !dir) dir = argv[i];
    }
    if (!dir) dir = getenv("PGDATA");
    if (!dir) { fprintf(stderr, "initdb: error: no data directory specified\n"); return 1; }
    if (!authlocal) authlocal = auth;
    if (!authhost) authhost = auth;

    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "initdb: error: could not create directory \"%s\": %s\n", dir, strerror(errno));
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/PG_VERSION", dir);
    if (access(path, F_OK) == 0) {
        fprintf(stderr, "initdb: error: directory \"%s\" exists but is not empty\n", dir);
        return 1;
    }
    mkd(dir, "base"); mkd(dir, "global"); mkd(dir, "pg_tblspc"); mkd(dir, "pg_twophase");
    mkd(dir, "pg_multixact"); mkd(dir, "pg_subtrans"); mkd(dir, "pg_snapshots");
    mkd(dir, "pg_stat"); mkd(dir, "pg_stat_tmp"); mkd(dir, "pg_logical");
    mkd(dir, "pg_replslot"); mkd(dir, "pg_serial"); mkd(dir, "pg_notify");
    mkd(dir, "pg_dynshmem");
#if PGVC_VN >= 100
    mkd(dir, "pg_wal"); mkd(dir, "pg_xact"); mkd(dir, "pg_commit_ts");
#else
    mkd(dir, "pg_xlog"); mkd(dir, "pg_clog"); mkd(dir, "pg_commit_ts");
#endif

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "initdb: error: could not write PG_VERSION\n"); return 1; }
#if PGVC_MAJOR == 96
    fprintf(f, "9.6\n");
#else
    fprintf(f, "%d\n", PGVC_MAJOR);
#endif
    fclose(f);

    snprintf(path, sizeof path, "%s/postgresql.conf", dir);
    f = fopen(path, "w");
    fprintf(f, "# -----------------------------\n"
               "# PostgreSQL configuration file\n"
               "# -----------------------------\n"
               "#listen_addresses = 'localhost'\n"
               "#port = 5432\n"
               "#unix_socket_directories = '/tmp'\n"
               "#datestyle = 'iso, mdy'\n"
               "#timezone = 'UTC'\n"
               "#lc_messages = 'C'\n");
    fclose(f);

    snprintf(path, sizeof path, "%s/pg_hba.conf", dir);
    f = fopen(path, "w");
    fprintf(f, "# TYPE  DATABASE        USER            ADDRESS                 METHOD\n");
    fprintf(f, "local   all             all                                     %s\n", authlocal);
    fprintf(f, "host    all             all             127.0.0.1/32            %s\n", authhost);
    fprintf(f, "host    all             all             ::1/128                 %s\n", authhost);
    fclose(f);

    snprintf(path, sizeof path, "%s/pg_ident.conf", dir);
    f = fopen(path, "w");
    fprintf(f, "# MAPNAME       SYSTEM-USERNAME         PG-USERNAME\n");
    fclose(f);

    snprintf(path, sizeof path, "%s/pgvc_bootstrap", dir);
    f = fopen(path, "w");
    fprintf(f, "superuser=%s\nencoding=%s\n", user, enc);
    fclose(f);

    printf("The files belonging to this database system will be owned by user \"%s\".\n", user);
    printf("\nSuccess. You can now start the database server using:\n\n"
           "    postgres -D %s\n\n", dir);
    return 0;
}
