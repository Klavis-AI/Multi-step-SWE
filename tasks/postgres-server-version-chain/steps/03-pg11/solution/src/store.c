/* store.c - persistence of the cluster across a clean restart.
 * The data directory keeps one plain-text dump per database under base/. */
#include "engine.h"
#include <sys/stat.h>
#include <dirent.h>

static void dump_value(Session *s, Buf *b, Datum d)
{
    if (d.isnull) { buf_adds(b, "\\N"); return; }
    char *t = out_func(s, d);
    for (char *q = t; *q; q++) {
        if (*q == '\\') buf_adds(b, "\\\\");
        else if (*q == '\n') buf_adds(b, "\\n");
        else if (*q == '\t') buf_adds(b, "\\t");
        else buf_addc(b, *q);
    }
    free(t);
}

void store_save(Database *db)
{
    if (!G || !G->datadir) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/base/%s.pgvc", G->datadir, db->name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    Session tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.db = db;
    for (int i = 0; i < db->nschemas; i++)
        fprintf(f, "S\t%s\t%s\n", db->schemas[i]->name, db->schemas[i]->owner);
    for (int i = 0; i < db->ntables; i++) {
        Table *t = db->tables[i];
        if (t->relkind == 'v') { fprintf(f, "V\t%s\t%s\t%s\n", t->schema, t->name, t->viewdef); continue; }
        if (t->relkind == 'S') {
            fprintf(f, "Q\t%s\t%s\t%lld\t%lld\t%lld\t%lld\t%d\n", t->schema, t->name,
                    (long long)t->seq_last, (long long)t->seq_start, (long long)t->seq_inc,
                    (long long)t->seq_max, t->seq_called);
            continue;
        }
        fprintf(f, "T\t%s\t%s\t%c\t%d\n", t->schema, t->name, t->relkind, t->ncols);
        for (int k = 0; k < t->ncols; k++)
            fprintf(f, "C\t%s\t%u\t%d\t%d\t%s\n", t->cols[k].name, t->cols[k].type,
                    t->cols[k].typmod, t->cols[k].notnull, t->cols[k].defexpr ? t->cols[k].defexpr : "");
        for (int r = 0; r < t->nrows; r++) {
            Row *row = t->rows[r];
            Buf b; buf_init(&b);
            buf_adds(&b, "R");
            for (int k = 0; k < t->ncols; k++) { buf_addc(&b, '\t'); dump_value(&tmp, &b, row->vals[k]); }
            fprintf(f, "%s\n", b.data);
            buf_free(&b);
        }
    }
    fclose(f);
}

void store_save_all(void)
{
    if (!G) return;
    for (int i = 0; i < G->ndbs; i++) store_save(G->dbs[i]);
}

void store_load(const char *datadir)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/base", datadir);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".pgvc")) continue;
        char dbname[256];
        snprintf(dbname, sizeof dbname, "%.*s", (int)(n - 5), e->d_name);
        Database *db = db_find(dbname);
        if (!db) db = db_create(dbname);
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[65536];
        Table *cur = NULL;
        Session tmp; memset(&tmp, 0, sizeof tmp); tmp.db = db;
        while (fgets(line, sizeof line, f)) {
            size_t l = strlen(line);
            while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            if (!l) continue;
            char *tok[16]; int nt = 0;
            char *p = line;
            while (nt < 16) {
                tok[nt++] = p;
                char *tab = strchr(p, '\t');
                if (!tab) break;
                *tab = '\0';
                p = tab + 1;
            }
            if (!strcmp(tok[0], "S") && nt >= 3) { if (!sch_find(db, tok[1])) sch_create(db, tok[1], tok[2]); }
            else if (!strcmp(tok[0], "T") && nt >= 5) {
                extern Table *store_mk_table(Database *db, const char *schema, const char *name, char kind);
                cur = store_mk_table(db, tok[1], tok[2], tok[3][0]);
            }
            else if (!strcmp(tok[0], "C") && cur && nt >= 6) {
                extern void store_add_col(Table *t, const char *name, Oid type, int32 typmod,
                                          bool notnull, const char *def);
                store_add_col(cur, tok[1], (Oid)strtoul(tok[2], NULL, 10), atoi(tok[3]),
                              atoi(tok[4]) != 0, *tok[5] ? tok[5] : NULL);
            }
            else if (!strcmp(tok[0], "R") && cur) {
                extern void store_add_row(Session *s, Table *t, char **vals, int n);
                store_add_row(&tmp, cur, tok + 1, nt - 1);
            }
            else if (!strcmp(tok[0], "V") && nt >= 4) {
                extern void store_add_view(Session *s, Database *db, const char *sch,
                                           const char *nm, const char *def);
                store_add_view(&tmp, db, tok[1], tok[2], tok[3]);
            }
            else if (!strcmp(tok[0], "Q") && nt >= 8) {
                extern void store_add_seq(Database *db, const char *sch, const char *nm,
                                          int64 last, int64 start, int64 inc, int64 mx, bool called);
                store_add_seq(db, tok[1], tok[2], strtoll(tok[3],NULL,10), strtoll(tok[4],NULL,10),
                              strtoll(tok[5],NULL,10), strtoll(tok[6],NULL,10), atoi(tok[7]) != 0);
            }
        }
        fclose(f);
    }
    closedir(d);
}
