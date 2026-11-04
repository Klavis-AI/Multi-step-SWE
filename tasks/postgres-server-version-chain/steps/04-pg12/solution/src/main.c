/* main.c - the `postgres` server program.
 * Command line and configuration follow the PostgreSQL documentation for
 * `postgres` and postgresql.conf / pg_hba.conf. */
#include "engine.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <signal.h>
#include <fcntl.h>

void serve_connection(int fd);
void guc_set_global(const char *name, const char *val);
void store_load(const char *datadir);
void store_save_all(void);

static char *g_listen = NULL;
static int   g_port = 5432;
static char *g_sockdir = NULL;
static char *g_datadir = NULL;

typedef struct { char *type, *db, *user, *addr, *method; } HbaLine;
static HbaLine hba[64];
static int nhba;

int hba_method_for(const char *user, const char *db, bool local)
{
    for (int i = 0; i < nhba; i++) {
        HbaLine *h = &hba[i];
        bool istype = local ? !strcmp(h->type, "local") : (!strcmp(h->type, "host") ||
                        !strcmp(h->type, "hostssl") || !strcmp(h->type, "hostnossl"));
        if (!istype) continue;
        if (strcmp(h->db, "all") && strcmp(h->db, db)) continue;
        if (strcmp(h->user, "all") && strcmp(h->user, user)) continue;
        if (!strcmp(h->method, "trust")) return 0;
        if (!strcmp(h->method, "md5")) return 1;
        if (!strcmp(h->method, "scram-sha-256")) return 2;
        if (!strcmp(h->method, "reject")) return 3;
        if (!strcmp(h->method, "password")) return 1;
        return 0;
    }
    return 0;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void load_hba(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/pg_hba.conf", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    nhba = 0;
    while (fgets(line, sizeof line, f) && nhba < 64) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        char *tok[8]; int n = 0;
        for (char *q = strtok(p, " \t"); q && n < 8; q = strtok(NULL, " \t")) tok[n++] = q;
        if (n < 4) continue;
        HbaLine *h = &hba[nhba++];
        memset(h, 0, sizeof *h);
        h->type = xstrdup(tok[0]); h->db = xstrdup(tok[1]); h->user = xstrdup(tok[2]);
        if (!strcmp(tok[0], "local")) { h->addr = xstrdup(""); h->method = xstrdup(tok[3]); }
        else { h->addr = xstrdup(tok[3]); h->method = xstrdup(n > 4 ? tok[4] : "trust"); }
    }
    fclose(f);
}

static void load_conf(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/postgresql.conf", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim(p), *v = trim(eq + 1);
        char *hash = strchr(v, '#');
        if (hash) { *hash = '\0'; v = trim(v); }
        size_t vl = strlen(v);
        if (vl >= 2 && v[0] == '\'' && v[vl-1] == '\'') { v[vl-1] = '\0'; v++; }
        if (!strcmp(k, "port")) g_port = atoi(v);
        else if (!strcmp(k, "listen_addresses")) g_listen = xstrdup(v);
        else if (!strcmp(k, "unix_socket_directories")) g_sockdir = xstrdup(v);
        else guc_set_global(k, v);
    }
    fclose(f);
}

static int listen_tcp(const char *addr, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (!addr || !*addr || !strcmp(addr, "*")) sa.sin_addr.s_addr = INADDR_ANY;
    else if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        fprintf(stderr, "could not bind IPv4 address \"%s\": %s\n", addr ? addr : "*", strerror(errno));
        close(fd); return -1;
    }
    listen(fd, 128);
    return fd;
}

static int listen_unix(const char *dir, int port)
{
    if (!dir || !*dir) return -1;
    char path[256];
    snprintf(path, sizeof path, "%s/.s.PGSQL.%d", dir, port);
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
    chmod(path, 0777);
    listen(fd, 128);
    return fd;
}

/* The predefined "pg_" roles, with the release each one appeared in. */
static void bootstrap_predefined_roles(void)
{
    static const struct { const char *name; Oid oid; int minvn; } pre[] = {
        {"pg_signal_backend",           4200, 0},
        {"pg_monitor",                  3373, 100},
        {"pg_read_all_settings",        3374, 100},
        {"pg_read_all_stats",           3375, 100},
        {"pg_stat_scan_tables",         3377, 100},
        {"pg_execute_server_program",   4571, 110},
        {"pg_read_server_files",        4569, 110},
        {"pg_write_server_files",       4570, 110},
        {"pg_read_all_data",            6181, 140},
        {"pg_write_all_data",           6182, 140},
        {"pg_database_owner",           6171, 140},
        {"pg_checkpoint",               4544, 150},
        {"pg_create_subscription",      6304, 160},
        {"pg_use_reserved_connections", 4550, 160},
        {"pg_maintain",                 6337, 170},
        {"pg_signal_autovacuum_worker", 6392, 180},
        {NULL,0,0}
    };
    for (int i = 0; pre[i].name; i++) {
        if (pre[i].minvn && PGVC_VN < pre[i].minvn) continue;
        if (role_find(pre[i].name)) continue;
        RoleEntry *e = xmalloc(sizeof *e);
        memset(e, 0, sizeof *e);
        e->oid = pre[i].oid;
        e->name = xstrdup(pre[i].name);
        e->inherit = true;
        G->roles = xrealloc(G->roles, (size_t)(G->nroles + 1) * sizeof(RoleEntry *));
        G->roles[G->nroles++] = e;
    }
}

static void bootstrap_roles(void)
{
    bootstrap_predefined_roles();
    if (!role_find("postgres")) {
        RoleEntry *e = xmalloc(sizeof *e);
        memset(e, 0, sizeof *e);
        e->oid = 10; e->name = xstrdup("postgres");
        e->canlogin = true; e->superuser = true; e->createdb = true;
        e->createrole = true; e->replication = true; e->inherit = true; e->bypassrls = true;
        G->roles = xrealloc(G->roles, (size_t)(G->nroles + 1) * sizeof(RoleEntry *));
        G->roles[G->nroles++] = e;
    }
    /* the predefined roles each release ships with */
    static const struct { const char *nm; Oid oid; int minvn; } pre[] = {
        {"pg_signal_backend",        4200, 0},
        {"pg_monitor",               3373, 100},
        {"pg_read_all_settings",     3374, 100},
        {"pg_read_all_stats",        3375, 100},
        {"pg_stat_scan_tables",      3377, 100},
        {"pg_read_server_files",     4569, 110},
        {"pg_write_server_files",    4570, 110},
        {"pg_execute_server_program",4571, 110},
        {"pg_database_owner",        6171, 140},
        {"pg_read_all_data",         6181, 140},
        {"pg_write_all_data",        6182, 140},
        {"pg_checkpoint",            4544, 150},
        {NULL,0,0}
    };
    for (int i = 0; pre[i].nm; i++) {
        if (pre[i].minvn && PGVC_VN < pre[i].minvn) continue;
        if (role_find(pre[i].nm)) continue;
        RoleEntry *e = xmalloc(sizeof *e);
        memset(e, 0, sizeof *e);
        e->oid = pre[i].oid; e->name = xstrdup(pre[i].nm);
        e->inherit = true;
        G->roles = xrealloc(G->roles, (size_t)(G->nroles + 1) * sizeof(RoleEntry *));
        G->roles[G->nroles++] = e;
    }
}

static volatile sig_atomic_t g_stop;
static void on_term(int sig) { (void)sig; g_stop = 1; }

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    char *setopts[64]; int nset = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            printf("postgres (PostgreSQL) %s\n", PGVC_VERSION_TEXT);
            return 0;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-?")) {
            printf("postgres is the PostgreSQL server.\n\n"
                   "Usage:\n  postgres [OPTION]...\n\nOptions:\n"
                   "  -D DATADIR  database directory\n"
                   "  -h HOSTNAME host name or IP address to listen on\n"
                   "  -p PORT     port number to listen on\n"
                   "  -k DIRECTORY Unix-domain socket location\n"
                   "  -c NAME=VALUE set run-time parameter\n");
            return 0;
        }
        if (!strcmp(argv[i], "-D") && i + 1 < argc) { g_datadir = argv[++i]; continue; }
        if (!strncmp(argv[i], "-D", 2) && argv[i][2]) { g_datadir = argv[i] + 2; continue; }
        if (!strcmp(argv[i], "-p") && i + 1 < argc) { g_port = atoi(argv[++i]); continue; }
        if (!strncmp(argv[i], "-p", 2) && argv[i][2]) { g_port = atoi(argv[i] + 2); continue; }
        if (!strcmp(argv[i], "-h") && i + 1 < argc) { g_listen = argv[++i]; continue; }
        if (!strncmp(argv[i], "-h", 2) && argv[i][2]) { g_listen = argv[i] + 2; continue; }
        if (!strcmp(argv[i], "-k") && i + 1 < argc) { g_sockdir = argv[++i]; continue; }
        if (!strncmp(argv[i], "-k", 2) && argv[i][2]) { g_sockdir = argv[i] + 2; continue; }
        if (!strcmp(argv[i], "-c") && i + 1 < argc) { if (nset < 64) setopts[nset++] = argv[++i]; continue; }
        if (!strncmp(argv[i], "-c", 2) && argv[i][2]) { if (nset < 64) setopts[nset++] = argv[i] + 2; continue; }
        if (!strcmp(argv[i], "-F") || !strcmp(argv[i], "-i") || !strcmp(argv[i], "-d")) continue;
        if (!strcmp(argv[i], "-N") || !strcmp(argv[i], "-B") || !strcmp(argv[i], "-S")) { i++; continue; }
    }
    if (!g_datadir) g_datadir = getenv("PGDATA");
    if (!g_datadir) {
        fprintf(stderr, "postgres: could not find the database system\n");
        return 1;
    }
    char vf[1024];
    snprintf(vf, sizeof vf, "%s/PG_VERSION", g_datadir);
    FILE *f = fopen(vf, "r");
    if (!f) {
        fprintf(stderr, "postgres: could not open file \"%s\": No such file or directory\n", vf);
        return 1;
    }
    fclose(f);

    G = xmalloc(sizeof *G);
    memset(G, 0, sizeof *G);
    G->datadir = xstrdup(g_datadir);
    G->next_oid = 16384;
    pthread_rwlock_init(&G->big, NULL);
    pthread_mutex_init(&G->mux, NULL);

    load_conf(g_datadir);
    load_hba(g_datadir);
    for (int i = 0; i < nset; i++) {
        char *eq = strchr(setopts[i], '=');
        if (!eq) continue;
        *eq = '\0';
        if (!strcmp(setopts[i], "port")) g_port = atoi(eq + 1);
        else if (!strcmp(setopts[i], "listen_addresses")) g_listen = xstrdup(eq + 1);
        else if (!strcmp(setopts[i], "unix_socket_directories")) g_sockdir = xstrdup(eq + 1);
        else guc_set_global(setopts[i], eq + 1);
        *eq = '=';
    }
    bootstrap_roles();
    store_load(g_datadir);
    if (!db_find("postgres")) db_create("postgres");
    if (!db_find("template1")) db_create("template1");
    if (!db_find("template0")) db_create("template0");

    int tfd = listen_tcp(g_listen ? g_listen : "127.0.0.1", g_port);
    int ufd = listen_unix(g_sockdir, g_port);
    if (tfd < 0 && ufd < 0) { fprintf(stderr, "postgres: no socket could be created\n"); return 1; }
    fprintf(stderr, "%s LOG:  database system is ready to accept connections\n", "pgvc");
    fflush(stderr);

    while (!g_stop) {
        fd_set rf;
        FD_ZERO(&rf);
        int mx = -1;
        if (tfd >= 0) { FD_SET(tfd, &rf); mx = tfd > mx ? tfd : mx; }
        if (ufd >= 0) { FD_SET(ufd, &rf); mx = ufd > mx ? ufd : mx; }
        struct timeval tv = { 1, 0 };
        int r = select(mx + 1, &rf, NULL, NULL, &tv);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (tfd >= 0 && FD_ISSET(tfd, &rf)) {
            int c = accept(tfd, NULL, NULL);
            if (c >= 0) serve_connection(c);
        }
        if (ufd >= 0 && FD_ISSET(ufd, &rf)) {
            int c = accept(ufd, NULL, NULL);
            if (c >= 0) serve_connection(c);
        }
    }
    store_save_all();
    return 0;
}
