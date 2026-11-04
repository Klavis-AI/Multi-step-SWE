/* catalog.c - the cluster's catalog and the pg_catalog / information_schema
 * relations that psql and drivers query. */
#include "engine.h"

Cluster *G;

/* ------------------------------------------------------------- lookups */
Database *db_find(const char *name)
{
    for (int i = 0; i < G->ndbs; i++)
        if (!strcmp(G->dbs[i]->name, name)) return G->dbs[i];
    return NULL;
}

Schema *sch_find(Database *db, const char *name)
{
    for (int i = 0; i < db->nschemas; i++)
        if (!strcmp(db->schemas[i]->name, name)) return db->schemas[i];
    return NULL;
}

Table *tbl_find(Database *db, const char *schema, const char *name)
{
    for (int i = 0; i < db->ntables; i++) {
        Table *t = db->tables[i];
        if (strcmp(t->name, name)) continue;
        if (schema) { if (!strcmp(t->schema, schema)) return t; }
        else if (!strcmp(t->schema, "public") || !strcmp(t->schema, "pg_catalog")) return t;
    }
    return NULL;
}

/* An unqualified name is resolved through search_path, exactly as the
 * PostgreSQL documentation describes. */
Table *tbl_find_sp(Session *s, const char *schema, const char *name)
{
    if (schema || !s) return tbl_find(s ? s->db : NULL, schema, name);
    const char *sp = guc_get(s, "search_path");
    if (!sp) sp = "\"$user\", public";
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", sp);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '"') tok++;
        char *e2 = tok + strlen(tok);
        while (e2 > tok && (e2[-1] == ' ' || e2[-1] == '"')) *--e2 = '\0';
        const char *sch = tok;
        if (!strcmp(tok, "$user")) sch = s->user ? s->user : "postgres";
        Table *t = tbl_find(s->db, sch, name);
        if (t) return t;
    }
    return tbl_find(s->db, "pg_catalog", name);
}

Table *tbl_find_oid(Database *db, Oid oid)
{
    for (int i = 0; i < db->ntables; i++) if (db->tables[i]->oid == oid) return db->tables[i];
    return NULL;
}

Index *idx_find(Database *db, const char *name)
{
    for (int i = 0; i < db->nindexes; i++)
        if (!strcmp(db->indexes[i]->name, name)) return db->indexes[i];
    return NULL;
}

int col_index(Table *t, const char *name)
{
    for (int i = 0; i < t->ncols; i++)
        if (!t->cols[i].dropped && !strcmp(t->cols[i].name, name)) return i;
    return -1;
}

RoleEntry *role_find(const char *name)
{
    for (int i = 0; i < G->nroles; i++)
        if (!strcmp(G->roles[i]->name, name)) return G->roles[i];
    return NULL;
}

Oid new_oid(void) { return ++G->next_oid; }

/* ------------------------------------------------------------------ ACLs.
 * PostgreSQL stores privileges as aclitem arrays; here an ACL is just the
 * letter string from the middle of an aclitem ("UC", "arwdDxt", ...). */
RoleEntry *role_by_oid(Oid oid)
{
    for (int i = 0; i < G->nroles; i++)
        if (G->roles[i]->oid == oid) return G->roles[i];
    return NULL;
}

const char *session_role(Session *s)
{
    if (s && s->eff_role) return s->eff_role;
    if (s && s->role) return s->role;
    return (s && s->user) ? s->user : "postgres";
}

bool acl_has(const char *acl, char p)
{
    return acl && strchr(acl, p) != NULL;
}

static void acl_add(char **aclp, char p)
{
    if (acl_has(*aclp, p)) return;
    size_t n = *aclp ? strlen(*aclp) : 0;
    char *r = xmalloc(n + 2);
    if (n) memcpy(r, *aclp, n);
    r[n] = p; r[n+1] = 0;
    free(*aclp); *aclp = r;
}

static void acl_del(char **aclp, char p)
{
    if (!*aclp) return;
    char *w = *aclp;
    for (char *q = *aclp; *q; q++) if (*q != p) *w++ = *q;
    *w = 0;
}

/* Turn the privilege list of a GRANT into letters and apply it. */
void acl_apply(char **aclp, const char *privs, bool grant, bool isschema)
{
    static const struct { const char *word; char letter; } tab[] = {
        {"select",'r'}, {"insert",'a'}, {"update",'w'}, {"delete",'d'},
        {"truncate",'D'}, {"references",'x'}, {"trigger",'t'}, {"execute",'X'},
        {"usage",'U'}, {"create",'C'}, {"connect",'c'}, {"temporary",'T'},
        {"temp",'T'}, {"maintain",'m'}, {NULL,0}
    };
    const char *all = isschema ? "UC" : "arwdDxt";
    if (!privs) privs = "";
    /* ALL [PRIVILEGES] */
    {
        const char *q = privs;
        while (*q && isspace((unsigned char)*q)) q++;
        if (!strncasecmp(q, "all", 3) &&
            (q[3] == 0 || isspace((unsigned char)q[3]))) {
            for (const char *a = all; *a; a++)
                if (grant) acl_add(aclp, *a); else acl_del(aclp, *a);
            return;
        }
    }
    const char *q = privs;
    while (*q) {
        while (*q && !isalpha((unsigned char)*q)) q++;
        const char *b = q;
        while (isalpha((unsigned char)*q)) q++;
        if (q == b) break;
        size_t n = (size_t)(q - b);
        for (int i = 0; tab[i].word; i++)
            if (strlen(tab[i].word) == n && !strncasecmp(tab[i].word, b, n)) {
                if (grant) acl_add(aclp, tab[i].letter);
                else acl_del(aclp, tab[i].letter);
            }
    }
}

static char **acl_slot(char ***grantee, char ***gacl, int *n, const char *role)
{
    for (int i = 0; i < *n; i++)
        if (!strcmp((*grantee)[i], role)) return &(*gacl)[i];
    *grantee = xrealloc(*grantee, (size_t)(*n + 1) * sizeof(char *));
    *gacl    = xrealloc(*gacl,    (size_t)(*n + 1) * sizeof(char *));
    (*grantee)[*n] = xstrdup(role);
    (*gacl)[*n] = NULL;
    (*n)++;
    return &(*gacl)[*n - 1];
}

static const char *acl_lookup(char **grantee, char **gacl, int n, const char *role)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(grantee[i], role)) return gacl[i];
    return NULL;
}

/* Is this role a superuser, or the implicit owner through pg_database_owner? */
static bool role_is_super(const char *role)
{
    RoleEntry *r = role_find(role);
    return r && r->superuser;
}

static bool owner_match(const char *owner, const char *role)
{
    if (!owner) return false;
    if (!strcmp(owner, role)) return true;
    /* the database owner is a member of pg_database_owner */
    if (!strcmp(owner, "pg_database_owner") && role_is_super(role)) return true;
    return false;
}

void schema_grant(Schema *sc, const char *role, const char *privs, bool grant)
{
    if (!strcmp(role, "public")) { acl_apply(&sc->acl, privs, grant, true); return; }
    char **slot = acl_slot(&sc->grantee, &sc->gacl, &sc->ngrants, role);
    acl_apply(slot, privs, grant, true);
}

bool schema_priv(Session *s, Schema *sc, const char *role, char p)
{
    if (!sc) return false;
    if (!role) role = session_role(s);
    /* the pseudo-role PUBLIC only ever has what was granted to PUBLIC */
    if (!strcmp(role, "public") && !role_find("public"))
        return acl_has(sc->acl, p);
    if (role_is_super(role) || owner_match(sc->owner, role)) return true;
    if (acl_has(acl_lookup(sc->grantee, sc->gacl, sc->ngrants, role), p)) return true;
    return acl_has(sc->acl, p);
}

void table_grant(Table *t, const char *role, const char *privs, bool grant)
{
    if (!strcmp(role, "public")) { acl_apply(&t->acl, privs, grant, false); return; }
    char **slot = acl_slot(&t->grantee, &t->gacl, &t->ngrants, role);
    acl_apply(slot, privs, grant, false);
}

bool table_priv(Session *s, Table *t, const char *role, char p)
{
    if (!t) return false;
    if (!role) role = session_role(s);
    if (!strcmp(role, "public") && !role_find("public"))
        return acl_has(t->acl, p);
    if (role_is_super(role) || owner_match(t->owner, role)) return true;
    if (acl_has(acl_lookup(t->grantee, t->gacl, t->ngrants, role), p)) return true;
    return acl_has(t->acl, p);
}

Schema *sch_create(Database *db, const char *name, const char *owner)
{
    Schema *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->oid = new_oid(); s->name = xstrdup(name); s->owner = xstrdup(owner ? owner : "postgres");
    /* the public schema keeps CREATE for PUBLIC until PostgreSQL 15 */
    if (!strcmp(name, "public")) s->acl = xstrdup(PGVC_VN >= 150 ? "U" : "UC");
    db->schemas = xrealloc(db->schemas, (size_t)(db->nschemas + 1) * sizeof(Schema *));
    db->schemas[db->nschemas++] = s;
    catalog_note(5 /* U_ADD_SCHEMA */, db, s);
    return s;
}

Database *db_create(const char *name)
{
    Database *db = xmalloc(sizeof *db);
    memset(db, 0, sizeof *db);
    db->name = xstrdup(name);
    G->dbs = xrealloc(G->dbs, (size_t)(G->ndbs + 1) * sizeof(Database *));
    G->dbs[G->ndbs++] = db;
    sch_create(db, "pg_catalog", "postgres");
    sch_create(db, "public", "pg_database_owner");
    sch_create(db, "information_schema", "postgres");
    sch_create(db, "pg_toast", "postgres");
    return db;
}

void catalog_note(int kind, Database *db, void *obj);
void catalog_note_table_state(Database *db, Table *t);

void tbl_add(Database *db, Table *t)
{
    db->tables = xrealloc(db->tables, (size_t)(db->ntables + 1) * sizeof(Table *));
    db->tables[db->ntables++] = t;
    catalog_note(1 /* U_ADD_TABLE */, db, t);
}

void tbl_drop(Database *db, Table *t)
{
    catalog_note(2 /* U_DEL_TABLE */, db, t);
    for (int i = 0; i < db->ntables; i++)
        if (db->tables[i] == t) {
            memmove(&db->tables[i], &db->tables[i+1], (size_t)(db->ntables - i - 1) * sizeof(Table *));
            db->ntables--;
            break;
        }
    for (int i = db->nindexes - 1; i >= 0; i--)
        if (db->indexes[i]->table == t->oid) {
            catalog_note(4 /* U_DEL_INDEX */, db, db->indexes[i]);
            memmove(&db->indexes[i], &db->indexes[i+1], (size_t)(db->nindexes - i - 1) * sizeof(Index *));
            db->nindexes--;
        }
}

/* regclass / regtype / regproc name resolution */
int64 reg_oid_for(Session *s, Oid regtype, const char *name)
{
    const char *bare = strrchr(name, '.');
    bare = bare ? bare + 1 : name;
    char *lower = lower_ident(bare);
    int64 r = -1;
    if (regtype == REGTYPEOID) {
        Oid t = type_by_name(lower);
        if (!t && !strcmp(lower, "int")) t = INT4OID;
        r = t ? (int64)t : -1;
    } else if (regtype == REGNAMESPACEOID) {
        Schema *sc = (s && s->db) ? sch_find(s->db, bare) : NULL;
        if (sc) r = sc->oid;
        else if (!strcmp(bare, "pg_catalog") || !strcmp(bare, "information_schema")) r = 11;
    } else if (regtype == REGROLEOID) {
        RoleEntry *e = role_find(bare);
        if (e) r = e->oid;
    } else if (regtype == REGCLASSOID) {
        if (s && s->db) {
            Table *t = tbl_find(s->db, NULL, bare);
            if (t) r = t->oid;
            else { Index *ix = idx_find(s->db, bare); if (ix) r = ix->oid; }
        }
        if (r < 0) {
            /* the system relations are addressable by name too */
            extern Oid sysrel_oid(const char *name);
            Oid so = sysrel_oid(bare);
            if (so) r = so;
        }
    } else {
        r = 1;
    }
    free(lower);
    return r;
}

char *reg_name_for(Session *s, Oid regtype, int64 oid)
{
    if (regtype == REGNAMESPACEOID && s && s->db) {
        for (int i = 0; i < s->db->nschemas; i++)
            if (s->db->schemas[i]->oid == (Oid)oid) return xstrdup(s->db->schemas[i]->name);
        return NULL;
    }
    if (regtype == REGROLEOID) {
        for (int i = 0; i < G->nroles; i++)
            if (G->roles[i]->oid == (Oid)oid) return xstrdup(G->roles[i]->name);
        return NULL;
    }
    if (regtype == REGTYPEOID) {
        char *f = format_type_str((Oid)oid, -1);
        return f;
    }
    if (regtype == REGCLASSOID && s && s->db) {
        Table *t = tbl_find_oid(s->db, (Oid)oid);
        if (t) return xstrdup(t->name);
        for (int i = 0; i < s->db->nindexes; i++)
            if (s->db->indexes[i]->oid == (Oid)oid) return xstrdup(s->db->indexes[i]->name);
    }
    if (regtype == REGCLASSOID) {
        extern const char *sysrel_name(Oid oid);
        const char *n = sysrel_name((Oid)oid);
        if (n) return xstrdup(n);
    }
    return NULL;
}

/* -------------------------------------------------------- result plumbing */
QueryResult *qr_new(void)
{
    QueryResult *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    return r;
}

void qr_addcol(QueryResult *r, const char *name, Oid type, int32 typmod)
{
    r->colnames = xrealloc(r->colnames, (size_t)(r->ncols + 1) * sizeof(char *));
    r->coltypes = xrealloc(r->coltypes, (size_t)(r->ncols + 1) * sizeof(Oid));
    r->coltypmods = xrealloc(r->coltypmods, (size_t)(r->ncols + 1) * sizeof(int32));
    r->colnames[r->ncols] = xstrdup(name);
    r->coltypes[r->ncols] = type;
    r->coltypmods[r->ncols] = typmod;
    r->ncols++;
}

void qr_addrow(QueryResult *r, Datum *vals)
{
    if (r->nrows == r->caprows) {
        r->caprows = r->caprows ? r->caprows * 2 : 16;
        r->rows = xrealloc(r->rows, (size_t)r->caprows * sizeof(Datum *));
    }
    r->rows[r->nrows++] = vals;
}

void qr_free(QueryResult *r) { (void)r; }

/* ------------------------------------------------------- system catalogs */
typedef struct { QueryResult *r; Session *s; Datum *row; int col; } SVB;

static void sv_begin(SVB *b, QueryResult *r, Session *s)
{
    b->r = r; b->s = s; b->row = NULL; b->col = 0;
}
static void sv_row(SVB *b)
{
    b->row = xmalloc((size_t)(b->r->ncols ? b->r->ncols : 1) * sizeof(Datum));
    for (int i = 0; i < b->r->ncols; i++) b->row[i] = d_null(b->r->coltypes[i]);
    b->col = 0;
}
static void sv_end(SVB *b) { qr_addrow(b->r, b->row); b->row = NULL; }
static void sv_i(SVB *b, int64 v) { b->row[b->col] = d_int(b->r->coltypes[b->col], v); b->col++; }
static void sv_b(SVB *b, bool v) { b->row[b->col] = d_bool(v); b->col++; }
static void sv_t(SVB *b, const char *v)
{
    if (!v) b->row[b->col] = d_null(b->r->coltypes[b->col]);
    else b->row[b->col] = d_text(b->r->coltypes[b->col], v);
    b->col++;
}
static void sv_c(SVB *b, char v) { char s[2] = {v, 0}; sv_t(b, s); }
static void sv_null(SVB *b) { b->col++; }
static void sv_d(SVB *b, Datum d) { b->row[b->col++] = d; }

/* attstattarget/stxstattarget carry -1 for "use default_statistics_target".
 * PostgreSQL 17 made both columns nullable and spells that default as NULL. */
static void sv_stattarget(SVB *b, int64 target)
{
#if PGVC_VN >= 170
    if (target < 0) { sv_null(b); return; }
#endif
    sv_i(b, target);
}

static Datum oidvec(Session *s, const int *v, int n, Oid elem)
{
    (void)s;
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = elem; a->ndim = n ? 1 : 0; a->dims[0] = n; a->lbound[0] = 1; a->nelem = n;
    a->elems = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
    for (int i = 0; i < n; i++) a->elems[i] = d_int(elem, v[i]);
    Datum d; memset(&d, 0, sizeof d);
    d.type = array_type_of(elem); d.v.arr = a;
    return d;
}

/* int2vector: an array datum that prints space-separated, like PostgreSQL. */
static Datum intvec(Session *s, const int *v, int n)
{
    Datum d = oidvec(s, v, n, INT2OID);
    d.type = INT2VECTOROID;
    return d;
}

/* Databases have no oid of their own here; pg_database hands them out by
 * position from the first user oid, and anything that references a database
 * has to agree with it. */
static Oid db_oid(Database *db)
{
    for (int i = 0; i < G->ndbs; i++) if (G->dbs[i] == db) return (Oid)(16384 + i);
    return 0;
}

Datum textarr_pub(Session *s, char **v, int n);
static Datum textarr(Session *s, char **v, int n)
{
    (void)s;
    ArrayVal *a = xmalloc(sizeof *a);
    memset(a, 0, sizeof *a);
    a->elemtype = TEXTOID; a->ndim = n ? 1 : 0; a->dims[0] = n; a->lbound[0] = 1; a->nelem = n;
    a->elems = xmalloc((size_t)(n ? n : 1) * sizeof(Datum));
    for (int i = 0; i < n; i++) a->elems[i] = d_text(TEXTOID, v[i]);
    Datum d; memset(&d, 0, sizeof d);
    d.type = TEXTARRAYOID; d.v.arr = a;
    return d;
}

char *index_def_text(Database *db, Index *ix);
char *constraint_def_text(Session *s, Database *db, Table *t, Constraint *c);

/* The system relations this server exposes, with the release each one first
 * appeared in.  One table drives pg_class, pg_attribute, to_regclass and the
 * regclass input function, so they cannot drift apart. */
typedef struct { const char *name; char relkind; int minvn; int maxvn; } SysRelDef;
static const SysRelDef sysrels[] = {
    {"pg_class",              'r',  0,   0},
    {"pg_attribute",          'r',  0,   0},
    {"pg_namespace",          'r',  0,   0},
    {"pg_type",               'r',  0,   0},
    {"pg_constraint",         'r',  0,   0},
    {"pg_index",              'r',  0,   0},
    {"pg_proc",               'r',  0,   0},
    {"pg_database",           'r',  0,   0},
    {"pg_authid",             'r',  0,   0},
    {"pg_inherits",           'r',  0,   0},
    {"pg_am",                 'r',  0,   0},
    {"pg_attrdef",            'r',  0,   0},
    {"pg_description",        'r',  0,   0},
    {"pg_depend",             'r',  0,   0},
    {"pg_trigger",            'r',  0,   0},
    {"pg_statistic",          'r',  0,   0},
    {"pg_operator",           'r',  0,   0},
    {"pg_language",           'r',  0,   0},
    {"pg_rewrite",            'r',  0,   0},
    {"pg_enum",               'r',  0,   0},
    {"pg_collation",          'r',  0,   0},
    {"pg_extension",          'r',  0,   0},
    {"pg_tablespace",         'r',  0,   0},
    {"pg_range",              'r',  0,   0},
    {"pg_publication",        'r',  100, 0},
    {"pg_publication_rel",    'r',  100, 0},
    {"pg_subscription",       'r',  100, 0},
    {"pg_subscription_rel",   'r',  100, 0},
    {"pg_partitioned_table",  'r',  100, 0},
    {"pg_sequence",           'r',  100, 0},
    {"pg_statistic_ext",      'r',  100, 0},
    {"pg_stats_ext",          'v',  100, 0},
    {"pg_statistic_ext_data", 'r',  120, 0},
    {"pg_auth_members",       'r',  0,   0},
    {"pg_aggregate",          'r',  0,   0},
    {"pg_settings",           'v',  0,   0},
    {"pg_roles",              'v',  0,   0},
    {"pg_user",               'v',  0,   0},
    {"pg_shadow",             'v',  0,   0},
    {"pg_replication_slots",  'v',  0,   0},
    {"pg_stat_activity",      'v',  0,   0},
    {"pg_stat_replication",   'v',  0,   0},
    {"pg_tables",             'v',  0,   0},
    {"pg_views",              'v',  0,   0},
    {"pg_indexes",            'v',  0,   0},
    {"pg_matviews",           'v',  0,   0},
    {"pg_rules",              'v',  0,   0},
    {"pg_policies",           'v',  0,   0},
    {"pg_locks",              'v',  0,   0},
    {"pg_cursors",            'v',  0,   0},
    {"pg_prepared_statements",'v',  0,   0},
    {"pg_sequences",          'v',  100, 0},
    {"pg_publication_tables", 'v',  100, 0},
    {"pg_hba_file_rules",     'v',  100, 0},
    {"pg_stat_progress_vacuum",'v', 0,   0},
    {"pg_stat_progress_create_index",'v', 120, 0},
    {"pg_stat_progress_cluster",'v', 120, 0},
    {"pg_stat_slru",          'v',  130, 0},
    {"pg_stat_progress_analyze",'v', 130, 0},
    {"pg_stat_progress_basebackup",'v', 130, 0},
    {"pg_shmem_allocations",  'v',  130, 0},
    {"pg_stat_wal",           'v',  140, 0},
    {"pg_stat_progress_copy", 'v',  140, 0},
    {"pg_stat_replication_slots",'v', 140, 0},
    {"pg_stat_subscription_stats",'v', 150, 0},
    {"pg_backend_memory_contexts",'v', 140, 0},
    {"pg_stat_io",            'v',  160, 0},
    {NULL,0,0,0}
};

/* The information_schema views this server answers for. */
static const char *isviews[] = {
    "schemata", "tables", "columns", "views", "sequences", "routines", "parameters",
    "table_constraints", "key_column_usage", "referential_constraints",
    "check_constraints", "constraint_column_usage", "domains", "triggers",
    "table_privileges", "column_privileges", "role_table_grants", "udt_privileges",
    "applicable_roles", "enabled_roles", "information_schema_catalog_name",
    "column_domain_usage", "column_udt_usage", "data_type_privileges",
    "domain_constraints", "element_types", "usage_privileges", "view_column_usage",
    "view_routine_usage", "view_table_usage", NULL
};

/* The builtins whose pg_proc row is inspected by name; everything else gets
 * the generic one-argument-returning-text placeholder. */
typedef struct { const char *name; int nargs; char vol; char par; Oid rettype;
                 Oid args[4]; } ProcDetail;
static const ProcDetail proc_details[] = {
    {"gen_random_uuid", 0, 'v', 's', UUIDOID, {0,0,0,0}},
    {"pg_get_wal_replay_pause_state", 0, 'v', 's', TEXTOID, {0,0,0,0}},
    {"pg_is_wal_replay_paused", 0, 'v', 's', BOOLOID, {0,0,0,0}},
    {"pg_is_in_recovery", 0, 'v', 's', BOOLOID, {0,0,0,0}},
    {"factorial",       1, 'i', 's', NUMERICOID, {NUMERICOID,0,0,0}},
    {"pg_backup_start", 2, 'v', 'r', PGLSNOID, {TEXTOID, BOOLOID, 0, 0}},
    {"pg_backup_stop",  1, 'v', 'r', RECORDOID, {BOOLOID, 0, 0, 0}},
    {NULL,0,0,0,0,{0,0,0,0}}
};
static const ProcDetail *proc_detail_for(const char *name)
{
    for (int i = 0; proc_details[i].name; i++)
        if (!strcmp(proc_details[i].name, name)) return &proc_details[i];
    return NULL;
}

static bool sysrel_live(const SysRelDef *d)
{
    if (d->minvn && PGVC_VN < d->minvn) return false;
    if (d->maxvn && PGVC_VN > d->maxvn) return false;
    return true;
}

/* System relation oids are stable within a run so regclass round-trips. */
Oid sysrel_oid(const char *name)
{
    for (int i = 0; sysrels[i].name; i++)
        if (!strcmp(sysrels[i].name, name))
            return sysrel_live(&sysrels[i]) ? (Oid)(1200 + i) : 0;
    return 0;
}
const char *sysrel_name(Oid oid)
{
    if (oid < 1200) return NULL;
    int i = (int)oid - 1200;
    for (int k = 0; sysrels[k].name; k++)
        if (k == i) return sysrel_live(&sysrels[k]) ? sysrels[k].name : NULL;
    return NULL;
}

static Oid schema_oid(Database *db, const char *name)
{
    Schema *s = sch_find(db, name);
    return s ? s->oid : 0;
}

/* The system relations, built on demand from live catalog state. */
QueryResult *sysview_scan(Session *s, const char *schema, const char *name, bool *found)
{
    Database *db = s->db;
    QueryResult *r = qr_new();
    SVB b; sv_begin(&b, r, s);
    *found = true;

    bool pgc = !schema || !strcmp(schema, "pg_catalog");
    bool isc = schema && !strcmp(schema, "information_schema");

    /* A system relation the target release does not have is simply not there. */
    if (pgc)
        for (int i = 0; sysrels[i].name; i++)
            if (!strcmp(sysrels[i].name, name) && !sysrel_live(&sysrels[i])) {
                *found = false;
                return NULL;
            }

    if (pgc && !strcmp(name, "pg_class")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"relname",NAMEOID,-1);
        qr_addcol(r,"relnamespace",OIDOID,-1); qr_addcol(r,"reltype",OIDOID,-1);
        qr_addcol(r,"relowner",OIDOID,-1); qr_addcol(r,"relam",OIDOID,-1);
        qr_addcol(r,"relpages",INT4OID,-1); qr_addcol(r,"reltuples",FLOAT4OID,-1);
        qr_addcol(r,"relhasindex",BOOLOID,-1); qr_addcol(r,"relisshared",BOOLOID,-1);
        qr_addcol(r,"relpersistence",CHAROID,-1); qr_addcol(r,"relkind",CHAROID,-1);
        qr_addcol(r,"relnatts",INT2OID,-1); qr_addcol(r,"relchecks",INT2OID,-1);
#if PGVC_VN < 120
        qr_addcol(r,"relhasoids",BOOLOID,-1);
#endif
        qr_addcol(r,"relhasrules",BOOLOID,-1); qr_addcol(r,"relhastriggers",BOOLOID,-1);
        qr_addcol(r,"relhassubclass",BOOLOID,-1); qr_addcol(r,"relrowsecurity",BOOLOID,-1);
#if PGVC_VN >= 100
        qr_addcol(r,"relispartition",BOOLOID,-1); qr_addcol(r,"relpartbound",TEXTOID,-1);
#endif
        qr_addcol(r,"relreplident",CHAROID,-1);
        qr_addcol(r,"reltablespace",OIDOID,-1);
        qr_addcol(r,"reloptions",TEXTARRAYOID,-1);
        qr_addcol(r,"relacl",TEXTARRAYOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            int nchk = 0;
            for (int k = 0; k < t->ncons; k++) if (t->cons[k]->contype == 'c') nchk++;
            bool hasidx = false, hassub = false;
            for (int k = 0; k < db->nindexes; k++) if (db->indexes[k]->table == t->oid) hasidx = true;
            for (int k = 0; k < db->ntables; k++) if (db->tables[k]->parent == t->oid) hassub = true;
            int nlive = 0;
            for (int k = 0; k < t->ncols; k++) if (!t->cols[k].dropped) nlive++;
            sv_row(&b);
            sv_i(&b, t->oid); sv_t(&b, t->name); sv_i(&b, schema_oid(db, t->schema));
            sv_i(&b, t->oid + 100000); sv_i(&b, 10);
            /* Tables gained a relam of their own (heap) only in PostgreSQL 12. */
            sv_i(&b, (t->relkind=='r'||t->relkind=='m'||t->relkind=='t')
                     ? (PGVC_VN >= 120 ? 2 : 0) : 0);
            sv_i(&b, 0); sv_d(&b, d_float(FLOAT4OID, 0));
            sv_b(&b, hasidx); sv_b(&b, false);
            sv_c(&b, 'p'); sv_c(&b, t->relkind);
            sv_i(&b, t->ncols); sv_i(&b, nchk);
#if PGVC_VN < 120
            sv_b(&b, false);
#endif
            sv_b(&b, false); sv_b(&b, false); sv_b(&b, hassub); sv_b(&b, false);
#if PGVC_VN >= 100
            sv_b(&b, t->parent != 0);
            sv_t(&b, t->parent ? t->partbound : NULL);
#endif
            sv_c(&b, 'd'); sv_i(&b, 0);
            if (t->nopts) {
                char **o = xmalloc((size_t)t->nopts * sizeof(char *));
                for (int k = 0; k < t->nopts; k++) {
                    Buf ob; buf_init(&ob);
                    buf_printf(&ob, "%s=%s", t->optname[k], t->optval[k]);
                    o[k] = buf_steal(&ob);
                }
                sv_d(&b, textarr(s, o, t->nopts));
                for (int k = 0; k < t->nopts; k++) free(o[k]);
                free(o);
            } else sv_null(&b);
            sv_null(&b);
            (void)nlive;
            sv_end(&b);
        }
        for (int i = 0; sysrels[i].name; i++) {
            if (!sysrel_live(&sysrels[i])) continue;
            bool isview = sysrels[i].relkind == 'v';
            sv_row(&b);
            sv_i(&b, 1200 + i); sv_t(&b, sysrels[i].name); sv_i(&b, schema_oid(db, "pg_catalog"));
            sv_i(&b, 0); sv_i(&b, 10);
            sv_i(&b, isview ? 0 : (PGVC_VN >= 120 ? 2 : 0));
            sv_i(&b, 0); sv_d(&b, d_float(FLOAT4OID, 0));
            sv_b(&b, false); sv_b(&b, false);
            sv_c(&b, 'p'); sv_c(&b, isview ? 'v' : 'r');
            sv_i(&b, 1); sv_i(&b, 0);
#if PGVC_VN < 120
            sv_b(&b, false);
#endif
            sv_b(&b, false); sv_b(&b, false); sv_b(&b, false); sv_b(&b, false);
#if PGVC_VN >= 100
            sv_b(&b, false); sv_null(&b);
#endif
            sv_c(&b, 'n'); sv_i(&b, 0); sv_null(&b); sv_null(&b);
            sv_end(&b);
        }
        for (int i = 0; i < db->nindexes; i++) {
            Index *ix = db->indexes[i];
            Table *t = tbl_find_oid(db, ix->table);
            sv_row(&b);
            sv_i(&b, ix->oid); sv_t(&b, ix->name);
            sv_i(&b, schema_oid(db, t ? t->schema : "public"));
            sv_i(&b, 0); sv_i(&b, 10); sv_i(&b, ix->am ? ix->am : 403);
            sv_i(&b, 1); sv_d(&b, d_float(FLOAT4OID, t ? (double)t->nrows : 0));
            sv_b(&b, false); sv_b(&b, false);
            sv_c(&b, 'p'); sv_c(&b, ix->partitioned ? 'I' : 'i');
            sv_i(&b, ix->nkeys + ix->nincl); sv_i(&b, 0);
#if PGVC_VN < 120
            sv_b(&b, false);
#endif
            sv_b(&b, false); sv_b(&b, false); sv_b(&b, false); sv_b(&b, false);
#if PGVC_VN >= 100
            sv_b(&b, ix->parent_index != 0); sv_null(&b);
#endif
            sv_c(&b, 'n'); sv_i(&b, 0);
            if (ix->nopts) {
                char **o = xmalloc((size_t)ix->nopts * sizeof(char *));
                for (int k = 0; k < ix->nopts; k++) {
                    Buf ob; buf_init(&ob);
                    buf_printf(&ob, "%s=%s", ix->optname[k], ix->optval[k]);
                    o[k] = buf_steal(&ob);
                }
                sv_d(&b, textarr(s, o, ix->nopts));
                for (int k = 0; k < ix->nopts; k++) free(o[k]);
                free(o);
            } else sv_null(&b);
            sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_namespace")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"nspname",NAMEOID,-1);
        qr_addcol(r,"nspowner",OIDOID,-1); qr_addcol(r,"nspacl",TEXTARRAYOID,-1);
        for (int i = 0; i < db->nschemas; i++) {
            Schema *sc = db->schemas[i];
            sv_row(&b);
            sv_i(&b, sc->oid); sv_t(&b, sc->name);
            sv_i(&b, (!strcmp(sc->name, "public") && PGVC_VN >= 150) ? 6171 : 10);
            if (sc->acl || sc->ngrants) {
                char *items[32]; int ni = 0;
                Buf ob; buf_init(&ob);
                buf_printf(&ob, "%s=UC/%s", sc->owner, sc->owner);
                items[ni++] = buf_steal(&ob);
                if (sc->acl && *sc->acl) {
                    Buf pb; buf_init(&pb);
                    buf_printf(&pb, "=%s/%s", sc->acl, sc->owner);
                    items[ni++] = buf_steal(&pb);
                }
                for (int k = 0; k < sc->ngrants && ni < 32; k++) {
                    if (!sc->gacl[k] || !*sc->gacl[k]) continue;
                    Buf gb; buf_init(&gb);
                    buf_printf(&gb, "%s=%s/%s", sc->grantee[k], sc->gacl[k], sc->owner);
                    items[ni++] = buf_steal(&gb);
                }
                sv_d(&b, textarr(s, items, ni));
                for (int k = 0; k < ni; k++) free(items[k]);
            } else sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_attribute")) {
        qr_addcol(r,"attrelid",OIDOID,-1); qr_addcol(r,"attname",NAMEOID,-1);
        qr_addcol(r,"atttypid",OIDOID,-1); qr_addcol(r,"attnum",INT2OID,-1);
        qr_addcol(r,"attnotnull",BOOLOID,-1); qr_addcol(r,"atthasdef",BOOLOID,-1);
        qr_addcol(r,"attidentity",CHAROID,-1);
#if PGVC_VN >= 120
        qr_addcol(r,"attgenerated",CHAROID,-1);
#endif
        qr_addcol(r,"attisdropped",BOOLOID,-1); qr_addcol(r,"atttypmod",INT4OID,-1);
        qr_addcol(r,"attlen",INT2OID,-1); qr_addcol(r,"attndims",INT4OID,-1);
        qr_addcol(r,"attstattarget",INT4OID,-1);
#if PGVC_VN >= 110
        qr_addcol(r,"atthasmissing",BOOLOID,-1); qr_addcol(r,"attmissingval",TEXTOID,-1);
#endif
#if PGVC_VN >= 140
        qr_addcol(r,"attcompression",CHAROID,-1);
#endif
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind == 'S') {
                /* a sequence's heap relation has PostgreSQL's three columns */
                static const char *sq[] = {"last_value","log_cnt","is_called",NULL};
                static const Oid  sqt[] = {INT8OID, INT8OID, BOOLOID};
                for (int k = 0; sq[k]; k++) {
                    sv_row(&b);
                    sv_i(&b, t->oid); sv_t(&b, sq[k]); sv_i(&b, sqt[k]); sv_i(&b, k + 1);
                    sv_b(&b, true); sv_b(&b, false); sv_c(&b, '\0');
#if PGVC_VN >= 120
                    sv_c(&b, '\0');
#endif
                    sv_b(&b, false); sv_i(&b, -1); sv_i(&b, 8); sv_i(&b, 0);
                    sv_stattarget(&b, -1);
#if PGVC_VN >= 110
                    sv_b(&b, false); sv_null(&b);
#endif
#if PGVC_VN >= 140
                    sv_c(&b, '\0');
#endif
                    sv_end(&b);
                }
                continue;
            }
            for (int k = 0; k < t->ncols; k++) {
                Column *c = &t->cols[k];
                sv_row(&b);
                sv_i(&b, t->oid); sv_t(&b, c->name); sv_i(&b, c->type); sv_i(&b, c->attnum);
                sv_b(&b, c->notnull); sv_b(&b, c->defexpr != NULL || c->genexpr != NULL);
                sv_c(&b, c->identity ? c->identity : '\0');
#if PGVC_VN >= 120
                sv_c(&b, c->generated ? c->generated : '\0');
#endif
                sv_b(&b, c->dropped); sv_i(&b, c->typmod);
                sv_i(&b, -1); sv_i(&b, type_is_array(c->type) ? 1 : 0);
                sv_stattarget(&b, -1);
#if PGVC_VN >= 110
                sv_b(&b, c->missingval != NULL); sv_t(&b, c->missingval);
#endif
#if PGVC_VN >= 140
                { char cc[2] = { c->compression, 0 };
                  sv_t(&b, c->compression ? cc : ""); }
#endif
                sv_end(&b);
            }
            /* the system columns every relation carries */
            if (t->relkind == 'r' || t->relkind == 'p' || t->relkind == 'm') {
                struct { const char *nm; int num; Oid ty; } sysc[] = {
                    {"ctid",-1,27}, {"oid",-2,OIDOID}, {"xmin",-3,28}, {"cmin",-4,29},
                    {"xmax",-5,28}, {"cmax",-6,29}, {"tableoid",-7,OIDOID}
                };
                for (int k = 0; k < 7; k++) {
                    if (sysc[k].num == -2 && PGVC_VN >= 120) continue;
                    sv_row(&b);
                    sv_i(&b, t->oid); sv_t(&b, sysc[k].nm); sv_i(&b, sysc[k].ty);
                    sv_i(&b, sysc[k].num);
                    sv_b(&b, true); sv_b(&b, false); sv_c(&b, '\0');
#if PGVC_VN >= 120
                    sv_c(&b, '\0');
#endif
                    sv_b(&b, false); sv_i(&b, -1); sv_i(&b, 4); sv_i(&b, 0);
                    sv_stattarget(&b, -1);
#if PGVC_VN >= 110
                    sv_b(&b, false); sv_null(&b);
#endif
                    sv_end(&b);
                }
            }
        }
        /* an index relation describes its own key columns */
        for (int i = 0; i < db->nindexes; i++) {
            Index *ix = db->indexes[i];
            Table *t = tbl_find_oid(db, ix->table);
            if (!t) continue;
            int an = 0;
            for (int k = 0; k < ix->nkeys + ix->nincl; k++) {
                int attn = (k < ix->nkeys) ? ix->keys[k] : ix->incl[k - ix->nkeys];
                const char *nm = (attn > 0 && attn <= t->ncols) ? t->cols[attn-1].name : "expr";
                Oid ty = (attn > 0 && attn <= t->ncols) ? t->cols[attn-1].type : TEXTOID;
                sv_row(&b);
                sv_i(&b, ix->oid); sv_t(&b, nm); sv_i(&b, ty); sv_i(&b, ++an);
                sv_b(&b, false); sv_b(&b, false); sv_c(&b, '\0');
#if PGVC_VN >= 120
                sv_c(&b, '\0');
#endif
                sv_b(&b, false); sv_i(&b, -1); sv_i(&b, -1); sv_i(&b, 0);
                sv_stattarget(&b, (ix->statset && ix->stattarget) ? ix->stattarget[an-1] : -1);
#if PGVC_VN >= 110
                sv_b(&b, false); sv_null(&b);
#endif
#if PGVC_VN >= 140
                sv_c(&b, '\0');
#endif
                sv_end(&b);
            }
        }
        /* the system relations describe themselves too */
        for (int i = 0; sysrels[i].name; i++) {
            if (!sysrel_live(&sysrels[i])) continue;
            if (!strcmp(sysrels[i].name, "pg_attribute")) continue;   /* avoid recursion */
            bool f2 = false;
            QueryResult *sub = sysview_scan(s, "pg_catalog", sysrels[i].name, &f2);
            if (!f2 || !sub) continue;
            int an = 0;
            for (int k = 0; k < sub->ncols; k++) {
                /* before PostgreSQL 12 a catalog's oid was a system column */
                if (PGVC_VN < 120 && sysrels[i].relkind == 'r' &&
                    !strcmp(sub->colnames[k], "oid")) continue;
                sv_row(&b);
                sv_i(&b, 1200 + i); sv_t(&b, sub->colnames[k]); sv_i(&b, sub->coltypes[k]);
                sv_i(&b, ++an);
                sv_b(&b, false); sv_b(&b, false); sv_c(&b, '\0');
#if PGVC_VN >= 120
                sv_c(&b, '\0');
#endif
                sv_b(&b, false); sv_i(&b, -1); sv_i(&b, -1); sv_i(&b, 0);
                sv_stattarget(&b, -1);
#if PGVC_VN >= 110
                sv_b(&b, false); sv_null(&b);
#endif
#if PGVC_VN >= 140
                sv_c(&b, '\0');
#endif
                sv_end(&b);
            }
        }
        {   /* pg_attribute's own columns, listed by hand */
            static const char *self[] = {"attrelid","attname","atttypid","attnum","attnotnull",
                "atthasdef","attidentity","attgenerated","attisdropped","atttypmod","attlen",
                "attndims","attstattarget","atthasmissing","attmissingval",NULL};
            Oid selfoid = sysrel_oid("pg_attribute");
            int n = 0;
            for (int k = 0; self[k]; k++) {
                if (PGVC_VN < 120 && !strcmp(self[k], "attgenerated")) continue;
                if (PGVC_VN < 110 && (!strcmp(self[k], "atthasmissing") ||
                                      !strcmp(self[k], "attmissingval"))) continue;
                sv_row(&b);
                sv_i(&b, selfoid); sv_t(&b, self[k]); sv_i(&b, TEXTOID); sv_i(&b, ++n);
                sv_b(&b, false); sv_b(&b, false); sv_c(&b, '\0');
#if PGVC_VN >= 120
                sv_c(&b, '\0');
#endif
                sv_b(&b, false); sv_i(&b, -1); sv_i(&b, -1); sv_i(&b, 0);
                sv_stattarget(&b, -1);
#if PGVC_VN >= 110
                sv_b(&b, false); sv_null(&b);
#endif
#if PGVC_VN >= 140
                sv_c(&b, '\0');
#endif
                sv_end(&b);
            }
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_sequence")) {
        qr_addcol(r,"seqrelid",OIDOID,-1); qr_addcol(r,"seqtypid",OIDOID,-1);
        qr_addcol(r,"seqstart",INT8OID,-1); qr_addcol(r,"seqincrement",INT8OID,-1);
        qr_addcol(r,"seqmax",INT8OID,-1); qr_addcol(r,"seqmin",INT8OID,-1);
        qr_addcol(r,"seqcache",INT8OID,-1); qr_addcol(r,"seqcycle",BOOLOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'S') continue;
            sv_row(&b);
            sv_i(&b, t->oid); sv_i(&b, t->seqtype ? t->seqtype : INT8OID);
            sv_i(&b, t->seq_start); sv_i(&b, t->seq_inc);
            sv_i(&b, t->seq_max); sv_i(&b, t->seq_min);
            sv_i(&b, t->seq_cache); sv_b(&b, t->seq_cycle);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_publication_rel")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"prpubid",OIDOID,-1);
        qr_addcol(r,"prrelid",OIDOID,-1);
#if PGVC_VN >= 150
        qr_addcol(r,"prqual",TEXTOID,-1); qr_addcol(r,"prattrs",INT2VECTOROID,-1);
#endif
        for (int i = 0; i < db->npubs; i++) {
            Publication *p = db->pubs[i];
            for (int k = 0; k < p->ntables; k++) {
                sv_row(&b);
                sv_i(&b, new_oid()); sv_i(&b, p->oid); sv_i(&b, p->tables[k]);
#if PGVC_VN >= 150
                if (p->where && p->where[k]) {
                    char *d2 = deparse_expr_src(s, p->where[k], NULL, false);
                    sv_t(&b, d2); free(d2);
                } else sv_null(&b);
                if (p->ncollist && p->ncollist[k] > 0) {
                    Table *pt = tbl_find_oid(db, p->tables[k]);
                    int *an = xmalloc((size_t)p->ncollist[k] * sizeof(int));
                    int nn = 0;
                    for (int q = 0; q < p->ncollist[k]; q++) {
                        if (!pt) continue;
                        for (int w = 0; w < pt->ncols; w++)
                            if (!pt->cols[w].dropped && !strcmp(pt->cols[w].name, p->collist[k][q]))
                                an[nn++] = pt->cols[w].attnum;
                    }
                    Datum d = intvec(s, an, nn);
                    sv_d(&b, d); free(an);
                } else sv_null(&b);
#endif
                sv_end(&b);
            }
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_subscription")) {
        /* Attribute order follows the catalog itself, which keeps the fixed
         * width columns ahead of the varlena ones; that is why every option
         * added after 10 lands in the middle of the row rather than at the
         * end.  15 brought two-phase apply, skip-LSN and disable_on_error, 16
         * the ownership and origin controls, 17 failover. */
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"subdbid",OIDOID,-1);
#if PGVC_VN >= 150
        qr_addcol(r,"subskiplsn",PGLSNOID,-1);
#endif
        qr_addcol(r,"subname",NAMEOID,-1); qr_addcol(r,"subowner",OIDOID,-1);
        qr_addcol(r,"subenabled",BOOLOID,-1);
#if PGVC_VN >= 140
        qr_addcol(r,"subbinary",BOOLOID,-1);
        /* streaming was a flag until 16 gave it a third state, "parallel" */
#if PGVC_VN >= 160
        qr_addcol(r,"substream",CHAROID,-1);
#else
        qr_addcol(r,"substream",BOOLOID,-1);
#endif
#endif
#if PGVC_VN >= 150
        qr_addcol(r,"subtwophasestate",CHAROID,-1);
        qr_addcol(r,"subdisableonerr",BOOLOID,-1);
#endif
#if PGVC_VN >= 160
        qr_addcol(r,"subpasswordrequired",BOOLOID,-1);
        qr_addcol(r,"subrunasowner",BOOLOID,-1);
#endif
#if PGVC_VN >= 170
        qr_addcol(r,"subfailover",BOOLOID,-1);
#endif
        qr_addcol(r,"subconninfo",TEXTOID,-1); qr_addcol(r,"subslotname",NAMEOID,-1);
        qr_addcol(r,"subsynccommit",TEXTOID,-1);
        qr_addcol(r,"subpublications",TEXTARRAYOID,-1);
#if PGVC_VN >= 160
        qr_addcol(r,"suborigin",TEXTOID,-1);
#endif
        for (int i = 0; i < db->nsubs; i++) {
            Subscription *sub = db->subs[i];
            sv_row(&b);
            sv_i(&b, sub->oid); sv_i(&b, db_oid(db));
#if PGVC_VN >= 150
            sv_t(&b, "0/0");
#endif
            sv_t(&b, sub->name); sv_i(&b, 10); sv_b(&b, sub->enabled);
#if PGVC_VN >= 140
            sv_b(&b, sub->binary);
#if PGVC_VN >= 160
            sv_c(&b, sub->stream);
#else
            sv_b(&b, sub->stream == 't');
#endif
#endif
#if PGVC_VN >= 150
            sv_c(&b, sub->twophase); sv_b(&b, sub->disableonerr);
#endif
#if PGVC_VN >= 160
            sv_b(&b, sub->passwordrequired); sv_b(&b, sub->runasowner);
#endif
#if PGVC_VN >= 170
            sv_b(&b, sub->failover);
#endif
            sv_t(&b, sub->conninfo); sv_t(&b, sub->slotname);
            sv_t(&b, sub->synccommit);
            sv_d(&b, textarr(s, sub->pubs, sub->npubs));
#if PGVC_VN >= 160
            sv_t(&b, sub->origin);
#endif
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_auth_members")) {
#if PGVC_VN >= 160
        qr_addcol(r,"oid",OIDOID,-1);
#endif
        qr_addcol(r,"roleid",OIDOID,-1); qr_addcol(r,"member",OIDOID,-1);
        qr_addcol(r,"grantor",OIDOID,-1); qr_addcol(r,"admin_option",BOOLOID,-1);
#if PGVC_VN >= 160
        qr_addcol(r,"inherit_option",BOOLOID,-1); qr_addcol(r,"set_option",BOOLOID,-1);
#endif
        return r;
    }
    if (pgc && !strcmp(name, "pg_aggregate")) {
        qr_addcol(r,"aggfnoid",OIDOID,-1); qr_addcol(r,"aggkind",CHAROID,-1);
        qr_addcol(r,"aggnumdirectargs",INT2OID,-1); qr_addcol(r,"aggtransfn",OIDOID,-1);
        qr_addcol(r,"aggfinalfn",OIDOID,-1); qr_addcol(r,"aggtranstype",OIDOID,-1);
        qr_addcol(r,"agginitval",TEXTOID,-1);
        int nb = 0;
        const ProcDef *pd = builtin_procs(&nb);
        for (int i = 0; i < nb; i++) {
            if (pd[i].kind != 'a') continue;
            if (pd[i].minvn && PGVC_VN < pd[i].minvn) continue;
            if (pd[i].maxvn && PGVC_VN > pd[i].maxvn) continue;
            /* ordered-set and hypothetical-set aggregates carry a different kind */
            char kind = 'n';
            if (!strcmp(pd[i].name,"percentile_cont") || !strcmp(pd[i].name,"percentile_disc") ||
                !strcmp(pd[i].name,"mode")) kind = 'o';
            else if (!strcmp(pd[i].name,"rank") || !strcmp(pd[i].name,"dense_rank") ||
                     !strcmp(pd[i].name,"percent_rank") || !strcmp(pd[i].name,"cume_dist"))
                kind = 'h';
            sv_row(&b);
            sv_i(&b, 1000 + i); sv_c(&b, kind); sv_i(&b, kind == 'n' ? 0 : 1);
            sv_i(&b, 0); sv_i(&b, 0); sv_i(&b, 2281); sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
#if PGVC_VN >= 150
    if (pgc && !strcmp(name, "pg_stat_subscription_stats")) {
        qr_addcol(r,"subid",OIDOID,-1); qr_addcol(r,"subname",NAMEOID,-1);
        qr_addcol(r,"apply_error_count",INT8OID,-1); qr_addcol(r,"sync_error_count",INT8OID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"confl_insert_exists",INT8OID,-1);
        qr_addcol(r,"confl_update_origin_differs",INT8OID,-1);
        qr_addcol(r,"confl_update_exists",INT8OID,-1);
        qr_addcol(r,"confl_update_missing",INT8OID,-1);
        qr_addcol(r,"confl_delete_origin_differs",INT8OID,-1);
        qr_addcol(r,"confl_delete_missing",INT8OID,-1);
        qr_addcol(r,"confl_multiple_unique_conflicts",INT8OID,-1);
#endif
        qr_addcol(r,"stats_reset",TIMESTAMPTZOID,-1);
        return r;
    }
#endif
#if PGVC_VN >= 160
    if (pgc && !strcmp(name, "pg_stat_io")) {
        qr_addcol(r,"backend_type",TEXTOID,-1); qr_addcol(r,"object",TEXTOID,-1);
        qr_addcol(r,"context",TEXTOID,-1); qr_addcol(r,"reads",INT8OID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"read_bytes",INT8OID,-1);
#endif
        qr_addcol(r,"read_time",FLOAT8OID,-1); qr_addcol(r,"writes",INT8OID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"write_bytes",INT8OID,-1);
#endif
        qr_addcol(r,"write_time",FLOAT8OID,-1); qr_addcol(r,"writebacks",INT8OID,-1);
        qr_addcol(r,"writeback_time",FLOAT8OID,-1); qr_addcol(r,"extends",INT8OID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"extend_bytes",INT8OID,-1);
#endif
        qr_addcol(r,"extend_time",FLOAT8OID,-1);
#if PGVC_VN < 180
        qr_addcol(r,"op_bytes",INT8OID,-1);
#endif
        qr_addcol(r,"hits",INT8OID,-1); qr_addcol(r,"evictions",INT8OID,-1);
        qr_addcol(r,"reuses",INT8OID,-1); qr_addcol(r,"fsyncs",INT8OID,-1);
        qr_addcol(r,"fsync_time",FLOAT8OID,-1); qr_addcol(r,"stats_reset",TIMESTAMPTZOID,-1);
        return r;
    }
#endif
    if (pgc && !strcmp(name, "pg_subscription_rel")) {
        qr_addcol(r,"srsubid",OIDOID,-1); qr_addcol(r,"srrelid",OIDOID,-1);
        qr_addcol(r,"srsubstate",CHAROID,-1); qr_addcol(r,"srsublsn",TEXTOID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_hba_file_rules")) {
        qr_addcol(r,"line_number",INT4OID,-1); qr_addcol(r,"type",TEXTOID,-1);
        qr_addcol(r,"database",TEXTARRAYOID,-1); qr_addcol(r,"user_name",TEXTARRAYOID,-1);
        qr_addcol(r,"address",TEXTOID,-1); qr_addcol(r,"netmask",TEXTOID,-1);
        qr_addcol(r,"auth_method",TEXTOID,-1); qr_addcol(r,"options",TEXTARRAYOID,-1);
        qr_addcol(r,"error",TEXTOID,-1);
        return r;
    }
#if PGVC_VN >= 100
    if (pgc && !strcmp(name, "pg_stats_ext")) {
        extern bool row_visible(Session *s, Row *r);
        qr_addcol(r,"schemaname",NAMEOID,-1); qr_addcol(r,"tablename",NAMEOID,-1);
        qr_addcol(r,"statistics_schemaname",NAMEOID,-1);
        qr_addcol(r,"statistics_name",NAMEOID,-1);
        qr_addcol(r,"statistics_owner",NAMEOID,-1);
        qr_addcol(r,"attnames",NAMEARRAYOID,-1);
        qr_addcol(r,"kinds",1002 /* "char"[] */,-1);
#if PGVC_VN >= 140
        qr_addcol(r,"exprs",TEXTARRAYOID,-1);
#endif
        qr_addcol(r,"n_distinct",TEXTOID,-1);
        qr_addcol(r,"dependencies",TEXTOID,-1);
        qr_addcol(r,"most_common_vals",TEXTARRAYOID,-1);
        qr_addcol(r,"most_common_val_nulls",BOOLARRAYOID,-1);
        qr_addcol(r,"most_common_freqs",FLOAT8ARRAYOID,-1);
        qr_addcol(r,"most_common_base_freqs",FLOAT8ARRAYOID,-1);
        for (int i = 0; i < db->nstats; i++) {
            ExtStat *e = db->stats[i];
            Table *t = tbl_find_oid(db, e->table);
            if (!t) continue;
            sv_row(&b);
            sv_t(&b, t->schema); sv_t(&b, t->name);
            sv_t(&b, e->schema); sv_t(&b, e->name); sv_t(&b, "postgres");
            { char **nm = xmalloc((size_t)(e->nkeys ? e->nkeys : 1) * sizeof(char *));
              for (int k = 0; k < e->nkeys; k++) nm[k] = t->cols[e->keys[k]-1].name;
              Datum d = textarr(s, nm, e->nkeys); d.v.arr->elemtype = NAMEOID;
              d.type = NAMEARRAYOID;
              sv_d(&b, d); free(nm); }
            { int n = (int)strlen(e->kinds);
              char **kk = xmalloc((size_t)(n ? n : 1) * sizeof(char *));
              char tmp[16][2];
              for (int q = 0; q < n && q < 16; q++) { tmp[q][0] = e->kinds[q]; tmp[q][1] = 0;
                                                      kk[q] = tmp[q]; }
              Datum d = textarr(s, kk, n); d.type = 1002; d.v.arr->elemtype = CHAROID;
              sv_d(&b, d); free(kk); }
#if PGVC_VN >= 140
            sv_null(&b);
#endif
            /* n_distinct: the real count of distinct tuples for every
             * combination of two or more of the statistics columns */
            if (strchr(e->kinds, 'd') && e->nkeys >= 2) {
                Buf nb; buf_init(&nb);
                buf_addc(&nb, '{');
                bool first = true;
                int nk = e->nkeys;
                for (int sz = 2; sz <= nk; sz++) {
                    int idx[16];
                    for (int q = 0; q < sz; q++) idx[q] = q;
                    for (;;) {
                        if (!first) buf_adds(&nb, ", ");
                        first = false;
                        buf_addc(&nb, '"');
                        for (int q = 0; q < sz; q++) {
                            if (q) buf_adds(&nb, ", ");
                            buf_printf(&nb, "%d", e->keys[idx[q]]);
                        }
                        buf_adds(&nb, "\": ");
                        /* count distinct combinations */
                        int nd = 0;
                        for (int r1 = 0; r1 < t->nrows; r1++) {
                            if (!row_visible(s, t->rows[r1])) continue;
                            bool dup = false;
                            for (int r2 = 0; r2 < r1 && !dup; r2++) {
                                if (!row_visible(s, t->rows[r2])) continue;
                                bool same = true;
                                for (int q = 0; q < sz && same; q++) {
                                    Datum x = t->rows[r1]->vals[e->keys[idx[q]]-1];
                                    Datum y = t->rows[r2]->vals[e->keys[idx[q]]-1];
                                    if (x.isnull != y.isnull ||
                                        (!x.isnull && d_cmp(s, x, y) != 0)) same = false;
                                }
                                if (same) dup = true;
                            }
                            if (!dup) nd++;
                        }
                        buf_printf(&nb, "%d", nd);
                        int q = sz - 1;
                        while (q >= 0 && idx[q] == nk - sz + q) q--;
                        if (q < 0) break;
                        idx[q]++;
                        for (int w = q + 1; w < sz; w++) idx[w] = idx[w-1] + 1;
                    }
                }
                buf_addc(&nb, '}');
                char *x = buf_steal(&nb); sv_t(&b, x); free(x);
            } else sv_null(&b);
            sv_null(&b); sv_null(&b); sv_null(&b); sv_null(&b); sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
#endif
    if (pgc && !strcmp(name, "pg_statistic_ext")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"stxrelid",OIDOID,-1);
        qr_addcol(r,"stxname",NAMEOID,-1); qr_addcol(r,"stxnamespace",OIDOID,-1);
        qr_addcol(r,"stxowner",OIDOID,-1);
#if PGVC_VN >= 130 && PGVC_VN < 170
        qr_addcol(r,"stxstattarget",INT4OID,-1);
#endif
        qr_addcol(r,"stxkeys",INT2VECTOROID,-1);
#if PGVC_VN >= 170
        /* 17 made stxstattarget nullable and, with that, moved it after
         * stxkeys in the catalog's column order. */
        qr_addcol(r,"stxstattarget",INT4OID,-1);
#endif
        qr_addcol(r,"stxkind",1002 /* "char"[] */,-1);
#if PGVC_VN >= 140
        qr_addcol(r,"stxexprs",TEXTOID,-1);
#endif
#if PGVC_VN < 120
        /* the computed statistics lived here until PostgreSQL 12 split them
         * out into pg_statistic_ext_data */
        qr_addcol(r,"stxndistinct",TEXTOID,-1); qr_addcol(r,"stxdependencies",TEXTOID,-1);
#endif
        for (int i = 0; i < db->nstats; i++) {
            ExtStat *e = db->stats[i];
            sv_row(&b);
            sv_i(&b, e->oid); sv_i(&b, e->table); sv_t(&b, e->name);
            sv_i(&b, schema_oid(db, e->schema)); sv_i(&b, 10);
#if PGVC_VN >= 130 && PGVC_VN < 170
            sv_i(&b, e->stattarget);
#endif
            sv_d(&b, intvec(s, e->keys, e->nkeys));
#if PGVC_VN >= 170
            sv_stattarget(&b, e->stattarget);
#endif
            { int n = (int)strlen(e->kinds);
              char **kk = xmalloc((size_t)(n ? n : 1) * sizeof(char *));
              char tmp[16][2];
              for (int q = 0; q < n && q < 16; q++) { tmp[q][0] = e->kinds[q]; tmp[q][1] = 0;
                                                      kk[q] = tmp[q]; }
              Datum d = textarr(s, kk, n); d.type = 1002; d.v.arr->elemtype = CHAROID;
              sv_d(&b, d); free(kk); }
#if PGVC_VN >= 140
            if (e->nexprs) {
                Buf eb; buf_init(&eb);
                for (int q = 0; q < e->nexprs; q++) {
                    if (q) buf_adds(&eb, ", ");
                    char *d2 = deparse_expr_src(s, e->exprs[q], NULL, false);
                    buf_adds(&eb, d2 ? d2 : e->exprs[q]);
                    free(d2);
                }
                char *x = buf_steal(&eb); sv_t(&b, x); free(x);
            } else sv_null(&b);
#endif
#if PGVC_VN < 120
            sv_null(&b); sv_null(&b);
#endif
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_statistic_ext_data")) {
        qr_addcol(r,"stxoid",OIDOID,-1); qr_addcol(r,"stxdndistinct",TEXTOID,-1);
        qr_addcol(r,"stxddependencies",TEXTOID,-1); qr_addcol(r,"stxdmcv",TEXTOID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_operator")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"oprname",NAMEOID,-1);
        qr_addcol(r,"oprnamespace",OIDOID,-1); qr_addcol(r,"oprkind",CHAROID,-1);
        qr_addcol(r,"oprleft",OIDOID,-1);
        qr_addcol(r,"oprright",OIDOID,-1); qr_addcol(r,"oprresult",OIDOID,-1);
        extern void operator_catalog_scan(Session *s, QueryResult *r, Oid nsp);
        operator_catalog_scan(s, r, schema_oid(db, "pg_catalog"));
        return r;
    }
    if (pgc && !strcmp(name, "pg_trigger")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"tgrelid",OIDOID,-1);
        qr_addcol(r,"tgname",NAMEOID,-1); qr_addcol(r,"tgfoid",OIDOID,-1);
        qr_addcol(r,"tgtype",INT2OID,-1); qr_addcol(r,"tgenabled",CHAROID,-1);
        qr_addcol(r,"tgisinternal",BOOLOID,-1); qr_addcol(r,"tgconstraint",OIDOID,-1);
        qr_addcol(r,"tgoldtable",NAMEOID,-1); qr_addcol(r,"tgnewtable",NAMEOID,-1);
        for (int i = 0; i < db->ntriggers; i++) {
            Trigger *t = db->triggers[i];
            /* tgtype packs the row flag, the timing and the event mask */
            int ty = (t->forrow ? 1 : 0) | (t->timing == 0 ? 2 : 0) |
                     ((t->events & 1) ? 4 : 0) | ((t->events & 4) ? 8 : 0) |
                     ((t->events & 2) ? 16 : 0) | ((t->events & 8) ? 32 : 0) |
                     (t->timing == 2 ? 64 : 0);
            sv_row(&b);
            sv_i(&b, t->oid); sv_i(&b, t->table); sv_t(&b, t->name); sv_i(&b, 0);
            sv_i(&b, ty); sv_c(&b, 'O');
            sv_b(&b, t->isclone && PGVC_VN < 140);
            sv_i(&b, 0);
            sv_t(&b, t->oldtable); sv_t(&b, t->newtable);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_language")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"lanname",NAMEOID,-1);
        qr_addcol(r,"lanispl",BOOLOID,-1);
        const char *ln[] = {"internal","c","sql","plpgsql",NULL};
        for (int i = 0; ln[i]; i++) {
            sv_row(&b); sv_i(&b, 12 + i); sv_t(&b, ln[i]); sv_b(&b, i == 3); sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_constraint")) {
#if PGVC_VN >= 120
        qr_addcol(r,"oid",OIDOID,-1);
#endif
        qr_addcol(r,"conname",NAMEOID,-1);
        qr_addcol(r,"connamespace",OIDOID,-1); qr_addcol(r,"contype",CHAROID,-1);
        qr_addcol(r,"condeferrable",BOOLOID,-1); qr_addcol(r,"condeferred",BOOLOID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"conenforced",BOOLOID,-1);
#endif
        qr_addcol(r,"convalidated",BOOLOID,-1);
        qr_addcol(r,"conrelid",OIDOID,-1); qr_addcol(r,"contypid",OIDOID,-1);
        qr_addcol(r,"conindid",OIDOID,-1);
#if PGVC_VN >= 110
        qr_addcol(r,"conparentid",OIDOID,-1);
#endif
        qr_addcol(r,"confrelid",OIDOID,-1);
        qr_addcol(r,"confupdtype",CHAROID,-1); qr_addcol(r,"confdeltype",CHAROID,-1);
        qr_addcol(r,"confmatchtype",CHAROID,-1);
        qr_addcol(r,"conislocal",BOOLOID,-1); qr_addcol(r,"coninhcount",INT2OID,-1);
        qr_addcol(r,"connoinherit",BOOLOID,-1);
#if PGVC_VN >= 180
        qr_addcol(r,"conperiod",BOOLOID,-1);
#endif
        qr_addcol(r,"conkey",INT2ARRAYOID,-1); qr_addcol(r,"confkey",INT2ARRAYOID,-1);
        qr_addcol(r,"conpfeqop",OIDARRAYOID,-1); qr_addcol(r,"conppeqop",OIDARRAYOID,-1);
        qr_addcol(r,"conffeqop",OIDARRAYOID,-1);
#if PGVC_VN >= 150
        qr_addcol(r,"confdelsetcols",INT2ARRAYOID,-1);
#endif
        qr_addcol(r,"conexclop",OIDARRAYOID,-1);
        qr_addcol(r,"conbin",TEXTOID,-1);
#if PGVC_VN < 120
        qr_addcol(r,"consrc",TEXTOID,-1);
#endif
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            for (int k = 0; k < t->ncons; k++) {
                Constraint *c = t->cons[k];
                /* PostgreSQL 18 materialises NOT NULL constraints in the catalog. */
                if (c->contype == 'n' && PGVC_VN < 180) continue;
                if (!c->oid) c->oid = new_oid();
                sv_row(&b);
#if PGVC_VN >= 120
                sv_i(&b, c->oid);
#endif
                sv_t(&b, c->name); sv_i(&b, schema_oid(db, t->schema));
                sv_c(&b, c->contype); sv_b(&b, c->deferrable); sv_b(&b, false);
#if PGVC_VN >= 180
                sv_b(&b, !c->not_enforced);
#endif
                sv_b(&b, !c->not_valid && !c->not_enforced);
                sv_i(&b, t->oid); sv_i(&b, 0); sv_i(&b, 0);
#if PGVC_VN >= 110
                sv_i(&b, 0);
#endif
                sv_i(&b, c->fk_reltable);
                sv_c(&b, c->fk_upd ? c->fk_upd : 'a'); sv_c(&b, c->fk_del ? c->fk_del : 'a');
                sv_c(&b, 's');
                sv_b(&b, c->inhcount ? c->islocal : true); sv_i(&b, c->inhcount);
                sv_b(&b, c->noinherit);
#if PGVC_VN >= 180
                sv_b(&b, c->fk_period || c->without_overlaps);
#endif
                if (c->contype == 'n') {
                    int one = c->nn_attnum;
                    sv_d(&b, oidvec(s, &one, 1, INT2OID));
                } else sv_d(&b, oidvec(s, c->keys, c->nkeys, INT2OID));
                sv_d(&b, c->nfkeys ? oidvec(s, c->fkeys, c->nfkeys, INT2OID) : d_null(INT2ARRAYOID));
                sv_null(&b); sv_null(&b); sv_null(&b);
#if PGVC_VN >= 150
                sv_null(&b);
#endif
                sv_null(&b);
                sv_t(&b, c->checksrc);
#if PGVC_VN < 120
                sv_t(&b, c->checksrc);
#endif
                sv_end(&b);
            }
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_index")) {
        qr_addcol(r,"indexrelid",OIDOID,-1); qr_addcol(r,"indrelid",OIDOID,-1);
        qr_addcol(r,"indnatts",INT2OID,-1);
#if PGVC_VN >= 110
        qr_addcol(r,"indnkeyatts",INT2OID,-1);
#endif
        qr_addcol(r,"indisunique",BOOLOID,-1);
#if PGVC_VN >= 150
        qr_addcol(r,"indnullsnotdistinct",BOOLOID,-1);
#endif
        qr_addcol(r,"indisprimary",BOOLOID,-1);
        qr_addcol(r,"indisvalid",BOOLOID,-1); qr_addcol(r,"indisready",BOOLOID,-1);
        qr_addcol(r,"indislive",BOOLOID,-1); qr_addcol(r,"indkey",INT2VECTOROID,-1);
        qr_addcol(r,"indexprs",TEXTOID,-1); qr_addcol(r,"indpred",TEXTOID,-1);
        for (int i = 0; i < db->nindexes; i++) {
            Index *ix = db->indexes[i];
            int *kk = xmalloc((size_t)(ix->nkeys + ix->nincl ? ix->nkeys + ix->nincl : 1) * sizeof(int));
            for (int k = 0; k < ix->nkeys; k++) kk[k] = ix->keys[k];
            for (int k = 0; k < ix->nincl; k++) kk[ix->nkeys + k] = ix->incl[k];
            sv_row(&b);
            sv_i(&b, ix->oid); sv_i(&b, ix->table);
            sv_i(&b, ix->nkeys + ix->nincl);
#if PGVC_VN >= 110
            sv_i(&b, ix->nkeys);
#endif
            sv_b(&b, ix->unique);
#if PGVC_VN >= 150
            sv_b(&b, ix->nulls_nd);
#endif
            sv_b(&b, ix->primary); sv_b(&b, ix->valid || !ix->oid);
            sv_b(&b, true); sv_b(&b, true);
            sv_d(&b, intvec(s, kk, ix->nkeys + ix->nincl));
            { bool anyexpr = false;
              if (ix->exprs) for (int k = 0; k < ix->nkeys; k++) if (ix->exprs[k]) anyexpr = true;
              if (anyexpr) { Buf eb; buf_init(&eb);
                  for (int k = 0; k < ix->nkeys; k++) if (ix->exprs[k]) {
                      if (eb.len) buf_adds(&eb, ", ");
                      buf_adds(&eb, ix->exprs[k]); }
                  char *t = buf_steal(&eb); sv_t(&b, t); free(t); }
              else sv_null(&b); }
            sv_t(&b, ix->predicate);
            free(kk);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_indexes")) {
        qr_addcol(r,"schemaname",NAMEOID,-1); qr_addcol(r,"tablename",NAMEOID,-1);
        qr_addcol(r,"indexname",NAMEOID,-1); qr_addcol(r,"tablespace",NAMEOID,-1);
        qr_addcol(r,"indexdef",TEXTOID,-1);
        for (int i = 0; i < db->nindexes; i++) {
            Index *ix = db->indexes[i];
            Table *t = tbl_find_oid(db, ix->table);
            if (!t) continue;
            /* the view only reached partitioned tables in PostgreSQL 12 */
            if (t->relkind == 'p' && PGVC_VN < 120) continue;
            sv_row(&b);
            sv_t(&b, t->schema); sv_t(&b, t->name); sv_t(&b, ix->name); sv_null(&b);
            char *d = index_def_text(db, ix);
            sv_t(&b, d); free(d);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_tables")) {
        qr_addcol(r,"schemaname",NAMEOID,-1); qr_addcol(r,"tablename",NAMEOID,-1);
        qr_addcol(r,"tableowner",NAMEOID,-1); qr_addcol(r,"tablespace",NAMEOID,-1);
        qr_addcol(r,"hasindexes",BOOLOID,-1); qr_addcol(r,"hasrules",BOOLOID,-1);
        qr_addcol(r,"hastriggers",BOOLOID,-1); qr_addcol(r,"rowsecurity",BOOLOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'r' && t->relkind != 'p') continue;
            sv_row(&b);
            sv_t(&b, t->schema); sv_t(&b, t->name); sv_t(&b, t->owner ? t->owner : "postgres");
            sv_null(&b); sv_b(&b, false); sv_b(&b, false); sv_b(&b, false); sv_b(&b, false);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_views")) {
        qr_addcol(r,"schemaname",NAMEOID,-1); qr_addcol(r,"viewname",NAMEOID,-1);
        qr_addcol(r,"viewowner",NAMEOID,-1); qr_addcol(r,"definition",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'v') continue;
            sv_row(&b);
            sv_t(&b, t->schema); sv_t(&b, t->name); sv_t(&b, t->owner ? t->owner : "postgres");
            sv_t(&b, t->viewdef);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_partitioned_table")) {
        qr_addcol(r,"partrelid",OIDOID,-1); qr_addcol(r,"partstrat",CHAROID,-1);
        qr_addcol(r,"partnatts",INT2OID,-1); qr_addcol(r,"partdefid",OIDOID,-1);
        qr_addcol(r,"partattrs",INT2VECTOROID,-1); qr_addcol(r,"partexprs",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (!t->partstrat) continue;
            Oid defid = 0;
            for (int k = 0; k < db->ntables; k++)
                if (db->tables[k]->parent == t->oid && db->tables[k]->pbound.is_default)
                    defid = db->tables[k]->oid;
            sv_row(&b);
            sv_i(&b, t->oid); sv_c(&b, t->partstrat); sv_i(&b, t->npartkey); sv_i(&b, defid);
            sv_d(&b, intvec(s, t->partkey, t->npartkey));
            { Buf eb; buf_init(&eb);
              for (int k = 0; k < t->npartkey; k++)
                  if (t->partkeyexpr && t->partkeyexpr[k]) {
                      if (eb.len) buf_adds(&eb, ", ");
                      buf_adds(&eb, t->partkeyexpr[k]); }
              if (eb.len) { char *x = buf_steal(&eb); sv_t(&b, x); free(x); }
              else sv_null(&b); }
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_inherits")) {
        qr_addcol(r,"inhrelid",OIDOID,-1); qr_addcol(r,"inhparent",OIDOID,-1);
        qr_addcol(r,"inhseqno",INT4OID,-1);
#if PGVC_VN >= 140
        qr_addcol(r,"inhdetachpending",BOOLOID,-1);
#endif
        for (int i = 0; i < db->ntables; i++)
            if (db->tables[i]->parent) {
                sv_row(&b);
                sv_i(&b, db->tables[i]->oid); sv_i(&b, db->tables[i]->parent); sv_i(&b, 1);
#if PGVC_VN >= 140
                sv_b(&b, false);
#endif
                sv_end(&b);
            }
        for (int i = 0; i < db->nindexes; i++)
            if (db->indexes[i]->parent_index) {
                sv_row(&b);
                sv_i(&b, db->indexes[i]->oid); sv_i(&b, db->indexes[i]->parent_index); sv_i(&b, 1);
#if PGVC_VN >= 140
                sv_b(&b, false);
#endif
                sv_end(&b);
            }
        return r;
    }
    if (pgc && (!strcmp(name, "pg_roles") || !strcmp(name, "pg_authid") ||
                !strcmp(name, "pg_user") || !strcmp(name, "pg_shadow"))) {
        bool authid = !strcmp(name, "pg_authid");
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"rolname",NAMEOID,-1);
        qr_addcol(r,"rolsuper",BOOLOID,-1); qr_addcol(r,"rolinherit",BOOLOID,-1);
        qr_addcol(r,"rolcreaterole",BOOLOID,-1); qr_addcol(r,"rolcreatedb",BOOLOID,-1);
        qr_addcol(r,"rolcanlogin",BOOLOID,-1); qr_addcol(r,"rolreplication",BOOLOID,-1);
        qr_addcol(r,"rolbypassrls",BOOLOID,-1); qr_addcol(r,"rolconnlimit",INT4OID,-1);
        qr_addcol(r,"rolpassword",TEXTOID,-1);
        for (int i = 0; i < G->nroles; i++) {
            RoleEntry *e = G->roles[i];
            sv_row(&b);
            sv_i(&b, e->oid); sv_t(&b, e->name);
            sv_b(&b, e->superuser); sv_b(&b, e->inherit); sv_b(&b, e->createrole);
            sv_b(&b, e->createdb); sv_b(&b, e->canlogin); sv_b(&b, e->replication);
            sv_b(&b, e->bypassrls); sv_i(&b, -1);
            sv_t(&b, authid ? e->password : (e->password ? "********" : NULL));
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_database")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"datname",NAMEOID,-1);
        qr_addcol(r,"datdba",OIDOID,-1); qr_addcol(r,"encoding",INT4OID,-1);
        qr_addcol(r,"datcollate",NAMEOID,-1); qr_addcol(r,"datctype",NAMEOID,-1);
        qr_addcol(r,"datistemplate",BOOLOID,-1); qr_addcol(r,"datallowconn",BOOLOID,-1);
        qr_addcol(r,"datconnlimit",INT4OID,-1); qr_addcol(r,"datfrozenxid",XIDOID,-1);
        qr_addcol(r,"datminmxid",XIDOID,-1); qr_addcol(r,"dattablespace",OIDOID,-1);
        qr_addcol(r,"datacl",TEXTARRAYOID,-1);
#if PGVC_VN >= 150
        /* The collation columns arrived with the ICU provider in 15; 16 added
         * daticurules, and 17 renamed daticulocale to the provider-neutral
         * datlocale and added dathasloginevt for login event triggers. */
        qr_addcol(r,"datlocprovider",CHAROID,-1);
#if PGVC_VN >= 170
        qr_addcol(r,"datlocale",TEXTOID,-1);
        qr_addcol(r,"dathasloginevt",BOOLOID,-1);
#else
        qr_addcol(r,"daticulocale",TEXTOID,-1);
#endif
#if PGVC_VN >= 160
        qr_addcol(r,"daticurules",TEXTOID,-1);
#endif
        qr_addcol(r,"datcollversion",TEXTOID,-1);
#endif
        for (int i = 0; i < G->ndbs; i++) {
            sv_row(&b);
            sv_i(&b, db_oid(G->dbs[i])); sv_t(&b, G->dbs[i]->name); sv_i(&b, 10); sv_i(&b, 6);
            sv_t(&b, "C"); sv_t(&b, "C");
            sv_b(&b, !strncmp(G->dbs[i]->name, "template", 8));
            sv_b(&b, true);
            sv_i(&b, -1); sv_i(&b, 0); sv_i(&b, 0); sv_i(&b, 1663);
            sv_null(&b);
#if PGVC_VN >= 150
            sv_c(&b, 'c'); sv_null(&b);
#if PGVC_VN >= 170
            sv_b(&b, false);
#endif
#if PGVC_VN >= 160
            sv_null(&b);
#endif
            sv_null(&b);
#endif
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_publication")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"pubname",NAMEOID,-1);
        qr_addcol(r,"pubowner",OIDOID,-1); qr_addcol(r,"puballtables",BOOLOID,-1);
        qr_addcol(r,"pubinsert",BOOLOID,-1); qr_addcol(r,"pubupdate",BOOLOID,-1);
        qr_addcol(r,"pubdelete",BOOLOID,-1);
#if PGVC_VN >= 110
        qr_addcol(r,"pubtruncate",BOOLOID,-1);
#endif
#if PGVC_VN >= 130
        qr_addcol(r,"pubviaroot",BOOLOID,-1);
#endif
#if PGVC_VN >= 180
        /* 18 can replicate generated columns; publish_generated_columns
         * records which kind, so the catalog column is a code, not a flag. */
        qr_addcol(r,"pubgencols",CHAROID,-1);
#endif
        for (int i = 0; i < db->npubs; i++) {
            Publication *p = db->pubs[i];
            sv_row(&b);
            sv_i(&b, p->oid); sv_t(&b, p->name); sv_i(&b, 10); sv_b(&b, p->alltables);
            sv_b(&b, p->pub_insert); sv_b(&b, p->pub_update); sv_b(&b, p->pub_delete);
#if PGVC_VN >= 110
            sv_b(&b, p->pub_truncate);
#endif
#if PGVC_VN >= 130
            sv_b(&b, p->viaroot);
#endif
#if PGVC_VN >= 180
            sv_c(&b, 'n');                    /* publish_generated_columns = none */
#endif
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_publication_tables")) {
        qr_addcol(r,"pubname",NAMEOID,-1); qr_addcol(r,"schemaname",NAMEOID,-1);
        qr_addcol(r,"tablename",NAMEOID,-1);
#if PGVC_VN >= 150
        qr_addcol(r,"attnames",TEXTARRAYOID,-1); qr_addcol(r,"rowfilter",TEXTOID,-1);
#endif
        for (int i = 0; i < db->npubs; i++) {
            Publication *p = db->pubs[i];
            if (p->alltables) {
                for (int k = 0; k < db->ntables; k++) {
                    Table *t = db->tables[k];
                    if (t->relkind != 'r' || !strcmp(t->schema, "pg_catalog")) continue;
                    sv_row(&b);
                    sv_t(&b, p->name); sv_t(&b, t->schema); sv_t(&b, t->name);
#if PGVC_VN >= 150
                    { char **nm = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(char *));
                      int n = 0;
                      for (int q = 0; q < t->ncols; q++) if (!t->cols[q].dropped) nm[n++] = t->cols[q].name;
                      sv_d(&b, textarr(s, nm, n)); free(nm); }
                    sv_null(&b);
#endif
                    sv_end(&b);
                }
                continue;
            }
            for (int k = 0; k < p->ntables; k++) {
                Table *t0 = tbl_find_oid(db, p->tables[k]);
                if (!t0) continue;
                /* A partitioned table is published as its leaf partitions
                 * unless publish_via_partition_root says otherwise. */
                Table *leaves[256]; int nleaf = 0;
                if (t0->relkind == 'p' && !p->viaroot) {
                    Table *q2[256]; int nq = 0;
                    q2[nq++] = t0;
                    for (int w = 0; w < nq; w++) {
                        bool haskid = false;
                        for (int z = 0; z < db->ntables && nq < 256; z++)
                            if (db->tables[z]->parent == q2[w]->oid) {
                                haskid = true; q2[nq++] = db->tables[z];
                            }
                        if (!haskid && q2[w] != t0 && nleaf < 256) leaves[nleaf++] = q2[w];
                        else if (!haskid && q2[w] == t0 && nleaf < 256) { }
                    }
                } else leaves[nleaf++] = t0;
                for (int li = 0; li < nleaf; li++) {
                Table *t = leaves[li];
                sv_row(&b);
                sv_t(&b, p->name); sv_t(&b, t->schema); sv_t(&b, t->name);
#if PGVC_VN >= 150
                if (p->ncollist && p->ncollist[k] > 0)
                    sv_d(&b, textarr(s, p->collist[k], p->ncollist[k]));   /* stored in order */
                else {
                    char **nm = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(char *));
                    int n = 0;
                    for (int q = 0; q < t->ncols; q++) if (!t->cols[q].dropped) nm[n++] = t->cols[q].name;
                    sv_d(&b, textarr(s, nm, n)); free(nm);
                }
                if (p->where && p->where[k]) {
                    char *d2 = deparse_expr_src(s, p->where[k], NULL, false);
                    sv_t(&b, d2); free(d2);
                } else sv_null(&b);
#endif
                sv_end(&b);
                }
            }
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_sequences")) {
        qr_addcol(r,"schemaname",NAMEOID,-1); qr_addcol(r,"sequencename",NAMEOID,-1);
        qr_addcol(r,"start_value",INT8OID,-1); qr_addcol(r,"min_value",INT8OID,-1);
        qr_addcol(r,"max_value",INT8OID,-1); qr_addcol(r,"increment_by",INT8OID,-1);
        qr_addcol(r,"cycle",BOOLOID,-1); qr_addcol(r,"cache_size",INT8OID,-1);
        qr_addcol(r,"last_value",INT8OID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'S') continue;
            sv_row(&b);
            sv_t(&b, t->schema); sv_t(&b, t->name);
            sv_i(&b, t->seq_start); sv_i(&b, t->seq_min); sv_i(&b, t->seq_max);
            sv_i(&b, t->seq_inc); sv_b(&b, t->seq_cycle); sv_i(&b, t->seq_cache);
            if (t->seq_called) sv_i(&b, t->seq_last); else sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_replication_slots")) {
        qr_addcol(r,"slot_name",NAMEOID,-1); qr_addcol(r,"plugin",NAMEOID,-1);
        qr_addcol(r,"slot_type",TEXTOID,-1); qr_addcol(r,"datoid",OIDOID,-1);
        qr_addcol(r,"database",NAMEOID,-1); qr_addcol(r,"temporary",BOOLOID,-1);
        qr_addcol(r,"active",BOOLOID,-1); qr_addcol(r,"active_pid",INT4OID,-1);
        qr_addcol(r,"xmin",INT8OID,-1); qr_addcol(r,"catalog_xmin",INT8OID,-1);
        qr_addcol(r,"restart_lsn",PGLSNOID,-1);
        qr_addcol(r,"confirmed_flush_lsn",PGLSNOID,-1);
#if PGVC_VN >= 130
        qr_addcol(r,"wal_status",TEXTOID,-1); qr_addcol(r,"safe_wal_size",INT8OID,-1);
#endif
#if PGVC_VN >= 140
        qr_addcol(r,"two_phase",BOOLOID,-1);
#endif
#if PGVC_VN >= 180
        qr_addcol(r,"two_phase_at",PGLSNOID,-1);
#endif
#if PGVC_VN >= 170
        qr_addcol(r,"inactive_since",TIMESTAMPTZOID,-1);
#endif
#if PGVC_VN >= 160
        qr_addcol(r,"conflicting",BOOLOID,-1);
#endif
#if PGVC_VN >= 170
        qr_addcol(r,"invalidation_reason",TEXTOID,-1);
        qr_addcol(r,"failover",BOOLOID,-1); qr_addcol(r,"synced",BOOLOID,-1);
#endif
        extern int repl_slots_scan(Session *s, QueryResult *r);
        repl_slots_scan(s, r);
        return r;
    }
    if (pgc && !strcmp(name, "pg_proc")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"proname",NAMEOID,-1);
        qr_addcol(r,"pronamespace",OIDOID,-1); qr_addcol(r,"proowner",OIDOID,-1);
        qr_addcol(r,"prolang",OIDOID,-1);
#if PGVC_VN >= 110
        qr_addcol(r,"prokind",CHAROID,-1);
#else
        qr_addcol(r,"proisagg",BOOLOID,-1); qr_addcol(r,"proiswindow",BOOLOID,-1);
#endif
        qr_addcol(r,"prosecdef",BOOLOID,-1); qr_addcol(r,"provolatile",CHAROID,-1);
        qr_addcol(r,"proparallel",CHAROID,-1);
        qr_addcol(r,"pronargs",INT2OID,-1); qr_addcol(r,"prorettype",OIDOID,-1);
        qr_addcol(r,"proretset",BOOLOID,-1); qr_addcol(r,"proargtypes",OIDVECTOROID,-1);
        qr_addcol(r,"proargnames",TEXTARRAYOID,-1);
        qr_addcol(r,"proargmodes",1002 /* "char"[] */,-1);
        qr_addcol(r,"prosrc",TEXTOID,-1);
        for (int i = 0; i < db->nroutines; i++) {
            SqlRoutine *p = db->routines[i];
            sv_row(&b);
            sv_i(&b, p->oid); sv_t(&b, p->name); sv_i(&b, schema_oid(db, p->schema));
            sv_i(&b, 10); sv_i(&b, p->lang && !strcmp(p->lang, "plpgsql") ? 15 : 14);
#if PGVC_VN >= 110
            sv_c(&b, p->prokind);
#else
            sv_b(&b, false); sv_b(&b, false);
#endif
            sv_b(&b, false); sv_c(&b, p->volatility ? p->volatility : 'v');
            sv_c(&b, 'u');
            /* pronargs counts the input arguments only */
            { int nin = 0;
              for (int k = 0; k < p->nargs; k++)
                  if (!p->argmodes || p->argmodes[k] != 'o') nin++;
              sv_i(&b, nin); }
            sv_i(&b, p->rettype); sv_b(&b, p->retset);
            { int *at = xmalloc((size_t)(p->nargs ? p->nargs : 1) * sizeof(int));
              for (int k = 0; k < p->nargs; k++) at[k] = (int)p->argtypes[k];
              Datum d = oidvec(s, at, p->nargs, OIDOID); d.type = OIDVECTOROID;
              sv_d(&b, d); free(at); }
            if (p->argnames) sv_d(&b, textarr(s, p->argnames, p->nargs)); else sv_null(&b);
            { bool any = false;
              for (int k = 0; k < p->nargs; k++) if (p->argmodes && p->argmodes[k] && p->argmodes[k] != 'i') any = true;
              if (!any) sv_null(&b);
              else {
                  char **mm = xmalloc((size_t)(p->nargs ? p->nargs : 1) * sizeof(char *));
                  char tmp[32][2];
                  for (int k = 0; k < p->nargs && k < 32; k++) {
                      tmp[k][0] = p->argmodes[k] ? p->argmodes[k] : 'i';
                      tmp[k][1] = 0; mm[k] = tmp[k];
                  }
                  Datum d = textarr(s, mm, p->nargs);
                  d.type = 1002; d.v.arr->elemtype = CHAROID;
                  sv_d(&b, d); free(mm);
              } }
            sv_t(&b, p->body);
            sv_end(&b);
        }
        int nb = 0;
        const ProcDef *pd = builtin_procs(&nb);
        for (int i = 0; i < nb; i++) {
            if (pd[i].minvn && PGVC_VN < pd[i].minvn) continue;
            if (pd[i].maxvn && PGVC_VN > pd[i].maxvn) continue;
            sv_row(&b);
            sv_i(&b, 1000 + i); sv_t(&b, pd[i].name); sv_i(&b, schema_oid(db, "pg_catalog"));
            sv_i(&b, 10); sv_i(&b, 12);
#if PGVC_VN >= 110
            sv_c(&b, pd[i].kind);
#else
            sv_b(&b, pd[i].kind == 'a'); sv_b(&b, pd[i].kind == 'w');
#endif
            const ProcDetail *pdd = proc_detail_for(pd[i].name);
            sv_b(&b, false);
            { static const char *volatile_procs[] = {"random","random_normal","uuidv4",
                "uuidv7","gen_random_uuid","nextval","setval","currval","clock_timestamp",
                "timeofday","pg_sleep","setseed","pg_create_logical_replication_slot",
                "pg_create_physical_replication_slot","pg_drop_replication_slot",
                "pg_sync_replication_slots","pg_log_standby_snapshot",
                "pg_stat_reset_replication_slot", NULL};
              static const char *stable_procs[] = {"now","current_setting","pg_input_is_valid",
                "pg_input_error_info","to_timestamp","pg_get_expr","pg_get_viewdef",
                "pg_get_constraintdef","pg_get_indexdef","pg_get_function_arguments",
                "pg_get_function_result","pg_get_userbyid","pg_typeof","version",
                "pg_column_toast_chunk_id","pg_basetype", NULL};
              char vol = 'i';
              for (int k = 0; volatile_procs[k]; k++)
                  if (!strcmp(volatile_procs[k], pd[i].name)) { vol = 'v'; break; }
              if (vol == 'i')
                  for (int k = 0; stable_procs[k]; k++)
                      if (!strcmp(stable_procs[k], pd[i].name)) { vol = 's'; break; }
              if (vol == 'i' && pdd) vol = pdd->vol;
              sv_c(&b, vol); }
            sv_c(&b, pdd ? pdd->par : 's');
            int na2 = pd[i].nargs1 ? pd[i].nargs1 - 1 : (pdd ? pdd->nargs : 1);
            Oid rt2 = pd[i].rettype ? pd[i].rettype : (pdd ? pdd->rettype : TEXTOID);
            sv_i(&b, na2);
            sv_i(&b, rt2); sv_b(&b, false);
            { int at2[4]; int na = (pdd && !pd[i].nargs1) ? pdd->nargs : 0;
              for (int k = 0; k < na && k < 4; k++) at2[k] = (int)pdd->args[k];
              if (pd[i].arg0) { at2[0] = (int)pd[i].arg0; na = 1; }
              Datum d = oidvec(s, at2, na > 4 ? 4 : na, OIDOID);
              d.type = OIDVECTOROID;
              if (d.v.arr) d.v.arr->lbound[0] = 0;
              sv_d(&b, d); }
            sv_null(&b); sv_null(&b); sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_type")) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"typname",NAMEOID,-1);
        qr_addcol(r,"typnamespace",OIDOID,-1); qr_addcol(r,"typtype",CHAROID,-1);
        qr_addcol(r,"typlen",INT2OID,-1); qr_addcol(r,"typelem",OIDOID,-1);
        qr_addcol(r,"typarray",OIDOID,-1); qr_addcol(r,"typrelid",OIDOID,-1);
        qr_addcol(r,"typcategory",CHAROID,-1);
#if PGVC_VN >= 140
        qr_addcol(r,"typsubscript",REGPROCOID,-1);
#endif
        extern void type_catalog_scan(QueryResult *r, Oid nsp);
        type_catalog_scan(r, schema_oid(db, "pg_catalog"));
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'r' && t->relkind != 'v' && t->relkind != 'p') continue;
            sv_row(&b);
            sv_i(&b, t->oid + 100000); sv_t(&b, t->name); sv_i(&b, schema_oid(db, t->schema));
            sv_c(&b, 'c'); sv_i(&b, -1); sv_i(&b, 0); sv_i(&b, 0); sv_i(&b, t->oid);
            sv_c(&b, 'C');
#if PGVC_VN >= 140
            sv_d(&b, reg_make(s, REGPROCOID, 0, "-"));
#endif
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_settings")) {
        qr_addcol(r,"name",TEXTOID,-1); qr_addcol(r,"setting",TEXTOID,-1);
        qr_addcol(r,"unit",TEXTOID,-1); qr_addcol(r,"category",TEXTOID,-1);
        qr_addcol(r,"short_desc",TEXTOID,-1); qr_addcol(r,"extra_desc",TEXTOID,-1);
        qr_addcol(r,"context",TEXTOID,-1); qr_addcol(r,"vartype",TEXTOID,-1);
        qr_addcol(r,"source",TEXTOID,-1); qr_addcol(r,"min_val",TEXTOID,-1);
        qr_addcol(r,"max_val",TEXTOID,-1); qr_addcol(r,"enumvals",TEXTARRAYOID,-1);
        qr_addcol(r,"boot_val",TEXTOID,-1); qr_addcol(r,"reset_val",TEXTOID,-1);
        qr_addcol(r,"sourcefile",TEXTOID,-1); qr_addcol(r,"sourceline",INT4OID,-1);
        qr_addcol(r,"pending_restart",BOOLOID,-1);
        extern void guc_scan(Session *s, QueryResult *r);
        guc_scan(s, r);
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_activity")) {
        qr_addcol(r,"datid",OIDOID,-1); qr_addcol(r,"datname",NAMEOID,-1);
        qr_addcol(r,"pid",INT4OID,-1);
#if PGVC_VN >= 130
        qr_addcol(r,"leader_pid",INT4OID,-1);
#endif
        qr_addcol(r,"usename",NAMEOID,-1);
        qr_addcol(r,"application_name",TEXTOID,-1); qr_addcol(r,"state",TEXTOID,-1);
        qr_addcol(r,"wait_event_type",TEXTOID,-1); qr_addcol(r,"wait_event",TEXTOID,-1);
        qr_addcol(r,"query",TEXTOID,-1);
        extern void stat_activity_scan(Session *s, QueryResult *r);
        stat_activity_scan(s, r);
        return r;
    }
    /* The statistics views this release grew.  They report live counters in
     * real PostgreSQL; here they are shaped correctly and mostly empty. */
    if (pgc && !strcmp(name, "pg_stat_progress_vacuum")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"datid",OIDOID,-1);
        qr_addcol(r,"datname",NAMEOID,-1); qr_addcol(r,"relid",OIDOID,-1);
        qr_addcol(r,"phase",TEXTOID,-1); qr_addcol(r,"heap_blks_total",INT8OID,-1);
        qr_addcol(r,"heap_blks_scanned",INT8OID,-1); qr_addcol(r,"heap_blks_vacuumed",INT8OID,-1);
        qr_addcol(r,"index_vacuum_count",INT8OID,-1); qr_addcol(r,"max_dead_tuples",INT8OID,-1);
        qr_addcol(r,"num_dead_tuples",INT8OID,-1);
        return r;
    }
#if PGVC_VN >= 120
    if (pgc && !strcmp(name, "pg_stat_progress_create_index")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"datid",OIDOID,-1);
        qr_addcol(r,"datname",NAMEOID,-1); qr_addcol(r,"relid",OIDOID,-1);
        qr_addcol(r,"index_relid",OIDOID,-1); qr_addcol(r,"command",TEXTOID,-1);
        qr_addcol(r,"phase",TEXTOID,-1); qr_addcol(r,"lockers_total",INT8OID,-1);
        qr_addcol(r,"lockers_done",INT8OID,-1); qr_addcol(r,"current_locker_pid",INT8OID,-1);
        qr_addcol(r,"blocks_total",INT8OID,-1); qr_addcol(r,"blocks_done",INT8OID,-1);
        qr_addcol(r,"tuples_total",INT8OID,-1); qr_addcol(r,"tuples_done",INT8OID,-1);
        qr_addcol(r,"partitions_total",INT8OID,-1); qr_addcol(r,"partitions_done",INT8OID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_progress_cluster")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"datid",OIDOID,-1);
        qr_addcol(r,"datname",NAMEOID,-1); qr_addcol(r,"relid",OIDOID,-1);
        qr_addcol(r,"command",TEXTOID,-1); qr_addcol(r,"phase",TEXTOID,-1);
        qr_addcol(r,"cluster_index_relid",OIDOID,-1);
        qr_addcol(r,"heap_tuples_scanned",INT8OID,-1);
        qr_addcol(r,"heap_tuples_written",INT8OID,-1);
        qr_addcol(r,"heap_blks_total",INT8OID,-1);
        qr_addcol(r,"heap_blks_scanned",INT8OID,-1);
        qr_addcol(r,"index_rebuild_count",INT8OID,-1);
        return r;
    }
#endif
#if PGVC_VN >= 130
    if (pgc && !strcmp(name, "pg_stat_slru")) {
        qr_addcol(r,"name",TEXTOID,-1); qr_addcol(r,"blks_zeroed",INT8OID,-1);
        qr_addcol(r,"blks_hit",INT8OID,-1); qr_addcol(r,"blks_read",INT8OID,-1);
        qr_addcol(r,"blks_written",INT8OID,-1); qr_addcol(r,"blks_exists",INT8OID,-1);
        qr_addcol(r,"flushes",INT8OID,-1); qr_addcol(r,"truncates",INT8OID,-1);
        qr_addcol(r,"stats_reset",TIMESTAMPTZOID,-1);
        static const char *slru[] = {
#if PGVC_VN >= 170
            "commit_timestamp","multixact_member","multixact_offset","notify",
            "serializable","subtransaction","transaction","other",
#else
            "CommitTs","MultiXactMember","MultiXactOffset","Notify",
            "Serial","Subtrans","Xact","other",
#endif
            NULL };
        for (int i = 0; slru[i]; i++) {
            sv_row(&b);
            sv_t(&b, slru[i]);
            for (int k = 0; k < 7; k++) sv_i(&b, 0);
            sv_null(&b);
            sv_end(&b);
        }
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_progress_analyze")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"datid",OIDOID,-1);
        qr_addcol(r,"datname",NAMEOID,-1); qr_addcol(r,"relid",OIDOID,-1);
        qr_addcol(r,"phase",TEXTOID,-1); qr_addcol(r,"sample_blks_total",INT8OID,-1);
        qr_addcol(r,"sample_blks_scanned",INT8OID,-1);
        qr_addcol(r,"ext_stats_total",INT8OID,-1); qr_addcol(r,"ext_stats_computed",INT8OID,-1);
        qr_addcol(r,"child_tables_total",INT8OID,-1); qr_addcol(r,"child_tables_done",INT8OID,-1);
        qr_addcol(r,"current_child_table_relid",OIDOID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_progress_basebackup")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"phase",TEXTOID,-1);
        qr_addcol(r,"backup_total",INT8OID,-1); qr_addcol(r,"backup_streamed",INT8OID,-1);
        qr_addcol(r,"tablespaces_total",INT8OID,-1);
        qr_addcol(r,"tablespaces_streamed",INT8OID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_shmem_allocations")) {
        qr_addcol(r,"name",TEXTOID,-1); qr_addcol(r,"off",INT8OID,-1);
        qr_addcol(r,"size",INT8OID,-1); qr_addcol(r,"allocated_size",INT8OID,-1);
        return r;
    }
#endif
#if PGVC_VN >= 140
    if (pgc && !strcmp(name, "pg_stat_wal")) {
        qr_addcol(r,"wal_records",INT8OID,-1); qr_addcol(r,"wal_fpi",INT8OID,-1);
        qr_addcol(r,"wal_bytes",NUMERICOID,-1); qr_addcol(r,"wal_buffers_full",INT8OID,-1);
#if PGVC_VN < 180
        /* PostgreSQL 18 moved WAL write/sync counting into pg_stat_io and
         * dropped these four from pg_stat_wal. */
        qr_addcol(r,"wal_write",INT8OID,-1); qr_addcol(r,"wal_sync",INT8OID,-1);
        qr_addcol(r,"wal_write_time",FLOAT8OID,-1); qr_addcol(r,"wal_sync_time",FLOAT8OID,-1);
#endif
        qr_addcol(r,"stats_reset",TIMESTAMPTZOID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_progress_copy")) {
        qr_addcol(r,"pid",INT4OID,-1); qr_addcol(r,"datid",OIDOID,-1);
        qr_addcol(r,"datname",NAMEOID,-1); qr_addcol(r,"relid",OIDOID,-1);
        qr_addcol(r,"command",TEXTOID,-1); qr_addcol(r,"type",TEXTOID,-1);
        qr_addcol(r,"bytes_processed",INT8OID,-1); qr_addcol(r,"bytes_total",INT8OID,-1);
        qr_addcol(r,"tuples_processed",INT8OID,-1); qr_addcol(r,"tuples_excluded",INT8OID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_stat_replication_slots")) {
        qr_addcol(r,"slot_name",TEXTOID,-1); qr_addcol(r,"spill_txns",INT8OID,-1);
        qr_addcol(r,"spill_count",INT8OID,-1); qr_addcol(r,"spill_bytes",INT8OID,-1);
        qr_addcol(r,"stream_txns",INT8OID,-1); qr_addcol(r,"stream_count",INT8OID,-1);
        qr_addcol(r,"stream_bytes",INT8OID,-1); qr_addcol(r,"total_txns",INT8OID,-1);
        qr_addcol(r,"total_bytes",INT8OID,-1); qr_addcol(r,"stats_reset",TIMESTAMPTZOID,-1);
        return r;
    }
    if (pgc && !strcmp(name, "pg_backend_memory_contexts")) {
        qr_addcol(r,"name",TEXTOID,-1); qr_addcol(r,"ident",TEXTOID,-1);
        qr_addcol(r,"parent",TEXTOID,-1); qr_addcol(r,"level",INT4OID,-1);
        qr_addcol(r,"total_bytes",INT8OID,-1); qr_addcol(r,"total_nblocks",INT8OID,-1);
        qr_addcol(r,"free_bytes",INT8OID,-1); qr_addcol(r,"free_chunks",INT8OID,-1);
        qr_addcol(r,"used_bytes",INT8OID,-1);
        return r;
    }
#endif
    if (pgc && (!strcmp(name, "pg_am"))) {
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"amname",NAMEOID,-1);
        qr_addcol(r,"amhandler",OIDOID,-1); qr_addcol(r,"amtype",CHAROID,-1);
        struct { Oid oid; const char *nm; char ty; int minvn; } ams[] = {
            {2,"heap",'t',120}, {403,"btree",'i',0}, {405,"hash",'i',0},
            {783,"gist",'i',0}, {2742,"gin",'i',0}, {3580,"brin",'i',0},
            {4000,"spgist",'i',0}, {0,NULL,0,0}
        };
        for (int i = 0; ams[i].nm; i++) {
            if (ams[i].minvn && PGVC_VN < ams[i].minvn) continue;
            sv_row(&b); sv_i(&b, ams[i].oid); sv_t(&b, ams[i].nm);
            sv_i(&b, 0); sv_c(&b, ams[i].ty); sv_end(&b);
        }
        return r;
    }
    if (pgc && (!strcmp(name,"pg_description") || !strcmp(name,"pg_depend") ||
                !strcmp(name,"pg_rewrite") ||
                !strcmp(name,"pg_enum") || !strcmp(name,"pg_collation") ||
                !strcmp(name,"pg_policies") || !strcmp(name,"pg_rules") ||
                !strcmp(name,"pg_matviews") || !strcmp(name,"pg_extension") ||
                !strcmp(name,"pg_tablespace") || !strcmp(name,"pg_locks") ||
                !strcmp(name,"pg_stat_replication") ||
                !strcmp(name,"pg_attrdef") || !strcmp(name,"pg_range") ||
                !strcmp(name,"pg_cursors") || !strcmp(name,"pg_prepared_statements"))) {
        /* Present but empty in this server; the columns these are queried by exist. */
        if (!strcmp(name,"pg_attrdef")) {
            qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"adrelid",OIDOID,-1);
            qr_addcol(r,"adnum",INT2OID,-1); qr_addcol(r,"adbin",TEXTOID,-1);
            for (int i = 0; i < db->ntables; i++) {
                Table *t = db->tables[i];
                for (int k = 0; k < t->ncols; k++) {
                    /* a generated column's expression lives here too */
                    const char *src = t->cols[k].generated ? t->cols[k].genexpr
                                                           : t->cols[k].defexpr;
                    if (!src) continue;
                    sv_row(&b); sv_i(&b, new_oid()); sv_i(&b, t->oid);
                    sv_i(&b, t->cols[k].attnum);
                    { char *d2 = deparse_expr_src(s, src, NULL, false);
                      sv_t(&b, d2); free(d2); }
                    sv_end(&b);
                }
            }
            return r;
        }
        if (!strcmp(name,"pg_range")) {
            qr_addcol(r,"rngtypid",OIDOID,-1); qr_addcol(r,"rngsubtype",OIDOID,-1);
            qr_addcol(r,"rngcollation",OIDOID,-1); qr_addcol(r,"rngsubopc",OIDOID,-1);
            qr_addcol(r,"rngcanonical",OIDOID,-1); qr_addcol(r,"rngsubdiff",OIDOID,-1);
#if PGVC_VN >= 140
            qr_addcol(r,"rngmultitypid",OIDOID,-1);
#endif
            static const struct { Oid rng, sub, mrng; } rr[] = {
                {INT4RANGEOID, INT4OID,        INT4MULTIRANGEOID},
                {NUMRANGEOID,  NUMERICOID,     NUMMULTIRANGEOID},
                {TSRANGEOID,   TIMESTAMPOID,   TSMULTIRANGEOID},
                {TSTZRANGEOID, TIMESTAMPTZOID, TSTZMULTIRANGEOID},
                {DATERANGEOID, DATEOID,        DATEMULTIRANGEOID},
                {INT8RANGEOID, INT8OID,        INT8MULTIRANGEOID},
                {0,0,0}
            };
            for (int i = 0; rr[i].rng; i++) {
                sv_row(&b);
                sv_i(&b, rr[i].rng); sv_i(&b, rr[i].sub);
                sv_i(&b, 0); sv_i(&b, 0); sv_i(&b, 0); sv_i(&b, 0);
#if PGVC_VN >= 140
                sv_i(&b, rr[i].mrng);
#endif
                sv_end(&b);
            }
            return r;
        }
        if (!strcmp(name,"pg_description") || !strcmp(name,"pg_depend")) {
            qr_addcol(r,"objoid",OIDOID,-1); qr_addcol(r,"classoid",OIDOID,-1);
            qr_addcol(r,"objsubid",INT4OID,-1); qr_addcol(r,"description",TEXTOID,-1);
            return r;
        }
        qr_addcol(r,"oid",OIDOID,-1); qr_addcol(r,"name",NAMEOID,-1);
        return r;
    }

    /* ------------------------------------------------ information_schema */
    if (isc && !strcmp(name, "schemata")) {
        qr_addcol(r,"catalog_name",NAMEOID,-1); qr_addcol(r,"schema_name",NAMEOID,-1);
        qr_addcol(r,"schema_owner",NAMEOID,-1);
        for (int i = 0; i < db->nschemas; i++) {
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, db->schemas[i]->name); sv_t(&b, db->schemas[i]->owner);
            sv_end(&b);
        }
        return r;
    }
    if (isc && !strcmp(name, "tables")) {
        qr_addcol(r,"table_catalog",NAMEOID,-1); qr_addcol(r,"table_schema",NAMEOID,-1);
        qr_addcol(r,"table_name",NAMEOID,-1); qr_addcol(r,"table_type",TEXTOID,-1);
        qr_addcol(r,"is_insertable_into",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind == 'i' || t->relkind == 'I' || t->relkind == 'S') continue;
            if (!strcmp(t->schema, "pg_catalog")) continue;
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->name);
            sv_t(&b, t->relkind == 'v' ? "VIEW" : "BASE TABLE");
            sv_t(&b, t->relkind == 'v' ? "NO" : "YES");
            sv_end(&b);
        }
        /* the catalog and the information schema list themselves too */
        for (int i = 0; sysrels[i].name; i++) {
            if (!sysrel_live(&sysrels[i])) continue;
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, "pg_catalog"); sv_t(&b, sysrels[i].name);
            sv_t(&b, sysrels[i].relkind == 'v' ? "VIEW" : "BASE TABLE");
            sv_t(&b, sysrels[i].relkind == 'v' ? "NO" : "YES");
            sv_end(&b);
        }
        for (int i = 0; isviews[i]; i++) {
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, "information_schema"); sv_t(&b, isviews[i]);
            sv_t(&b, "VIEW"); sv_t(&b, "NO");
            sv_end(&b);
        }
        return r;
    }
    if (isc && !strcmp(name, "columns")) {
        qr_addcol(r,"table_catalog",NAMEOID,-1); qr_addcol(r,"table_schema",NAMEOID,-1);
        qr_addcol(r,"table_name",NAMEOID,-1); qr_addcol(r,"column_name",NAMEOID,-1);
        qr_addcol(r,"ordinal_position",INT4OID,-1); qr_addcol(r,"column_default",TEXTOID,-1);
        qr_addcol(r,"is_nullable",TEXTOID,-1); qr_addcol(r,"data_type",TEXTOID,-1);
        qr_addcol(r,"character_maximum_length",INT4OID,-1);
        qr_addcol(r,"numeric_precision",INT4OID,-1); qr_addcol(r,"numeric_scale",INT4OID,-1);
        qr_addcol(r,"datetime_precision",INT4OID,-1);
        qr_addcol(r,"udt_name",NAMEOID,-1);
        qr_addcol(r,"is_identity",TEXTOID,-1); qr_addcol(r,"identity_generation",TEXTOID,-1);
        qr_addcol(r,"identity_start",TEXTOID,-1); qr_addcol(r,"identity_increment",TEXTOID,-1);
        qr_addcol(r,"identity_maximum",TEXTOID,-1); qr_addcol(r,"identity_minimum",TEXTOID,-1);
        qr_addcol(r,"identity_cycle",TEXTOID,-1);
        qr_addcol(r,"is_generated",TEXTOID,-1); qr_addcol(r,"generation_expression",TEXTOID,-1);
        qr_addcol(r,"is_updatable",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind == 'i' || t->relkind == 'I' || t->relkind == 'S') continue;
            if (!strcmp(t->schema, "pg_catalog")) continue;
            for (int k = 0; k < t->ncols; k++) {
                Column *c = &t->cols[k];
                if (c->dropped) continue;
                int pos = c->attnum;
                sv_row(&b);
                sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->name); sv_t(&b, c->name);
                { extern char *deparse_default(Session *s, Oid coltype, const char *src);
                  char *dp = deparse_default(s, c->type, c->defexpr);
                  sv_i(&b, pos); sv_t(&b, dp); free(dp); }
                sv_t(&b, c->notnull ? "NO" : "YES");
                { char *ft = format_type_str(c->type, -1);
                  const char *dt = ft;
                  if (c->type == TIMESTAMPTZOID) dt = "timestamp with time zone";
                  else if (c->type == TIMESTAMPOID) dt = "timestamp without time zone";
                  else if (c->type == TIMEOID) dt = "time without time zone";
                  else if (type_is_array(c->type)) dt = "ARRAY";
                  sv_t(&b, dt); free(ft); }
                if ((c->type == VARCHAROID || c->type == BPCHAROID) && c->typmod >= 0)
                    sv_i(&b, c->typmod - 4);
                else sv_null(&b);
                if (c->type == NUMERICOID) {
                    if (c->typmod >= 0) { sv_i(&b, ((c->typmod - 4) >> 16) & 0xffff);
                                          sv_i(&b, (c->typmod - 4) & 0xffff); }
                    else { sv_null(&b); sv_null(&b); }
                } else if (c->type == INT2OID) { sv_i(&b, 16); sv_i(&b, 0); }
                else if (c->type == INT4OID) { sv_i(&b, 32); sv_i(&b, 0); }
                else if (c->type == INT8OID) { sv_i(&b, 64); sv_i(&b, 0); }
                else if (c->type == FLOAT4OID) { sv_i(&b, 24); sv_null(&b); }
                else if (c->type == FLOAT8OID) { sv_i(&b, 53); sv_null(&b); }
                else { sv_null(&b); sv_null(&b); }
                if (c->type == TIMESTAMPOID || c->type == TIMESTAMPTZOID || c->type == TIMEOID ||
                    c->type == INTERVALOID) sv_i(&b, c->typmod >= 0 ? c->typmod - 4 : 6);
                else sv_null(&b);
                sv_t(&b, type_name(c->type));
                sv_t(&b, c->identity ? "YES" : "NO");
                sv_t(&b, c->identity == 'a' ? "ALWAYS" : c->identity == 'd' ? "BY DEFAULT" : NULL);
                { Table *sq = c->identity ? tbl_find(db, t->schema, c->identity_seq) : NULL;
                  char nb[32];
                  if (sq) { snprintf(nb, sizeof nb, "%lld", (long long)sq->seq_start); sv_t(&b, nb); }
                  else sv_null(&b);
                  if (sq) { snprintf(nb, sizeof nb, "%lld", (long long)sq->seq_inc); sv_t(&b, nb); }
                  else sv_null(&b);
                  if (sq) { snprintf(nb, sizeof nb, "%lld", (long long)sq->seq_max); sv_t(&b, nb); }
                  else sv_null(&b);
                  if (sq) { snprintf(nb, sizeof nb, "%lld", (long long)sq->seq_min); sv_t(&b, nb); }
                  else sv_null(&b);
                  /* identity_cycle is NO even for a plain column */
                  sv_t(&b, (sq && sq->seq_cycle) ? "YES" : "NO"); }
                sv_t(&b, c->generated ? "ALWAYS" : "NEVER");
                { char *g2 = c->generated && c->genexpr
                      ? deparse_expr_src(s, c->genexpr, NULL, false) : NULL;
                  sv_t(&b, g2); free(g2); }
                sv_t(&b, t->relkind == 'v' ? "NO" : "YES");
                sv_end(&b);
            }
        }
        /* To the information schema a catalog is just another relation in a
         * schema, so the system relations are listed here as well; that is how
         * a client asks which catalog columns this release has. */
        for (int i = 0; sysrels[i].name; i++) {
            if (!sysrel_live(&sysrels[i])) continue;
            bool f2 = false;
            QueryResult *sub = sysview_scan(s, "pg_catalog", sysrels[i].name, &f2);
            if (!f2 || !sub) continue;
            int pos = 0;
            for (int k = 0; k < sub->ncols; k++) {
                /* before 12 a catalog's oid was a system column, and the
                 * information schema never shows those */
                if (PGVC_VN < 120 && sysrels[i].relkind == 'r' &&
                    !strcmp(sub->colnames[k], "oid")) continue;
                sv_row(&b);
                sv_t(&b, db->name); sv_t(&b, "pg_catalog"); sv_t(&b, sysrels[i].name);
                sv_t(&b, sub->colnames[k]);
                sv_i(&b, ++pos); sv_null(&b); sv_t(&b, "YES");
                { Oid ct = sub->coltypes[k];
                  char *ft = format_type_str(ct, -1);
                  const char *dt = ft;
                  if (ct == TIMESTAMPTZOID) dt = "timestamp with time zone";
                  else if (ct == TIMESTAMPOID) dt = "timestamp without time zone";
                  else if (ct == TIMEOID) dt = "time without time zone";
                  else if (type_is_array(ct)) dt = "ARRAY";
                  sv_t(&b, dt); free(ft); }
                sv_null(&b);
                { Oid ct = sub->coltypes[k];
                  if (ct == INT2OID) { sv_i(&b, 16); sv_i(&b, 0); }
                  else if (ct == INT4OID) { sv_i(&b, 32); sv_i(&b, 0); }
                  else if (ct == INT8OID) { sv_i(&b, 64); sv_i(&b, 0); }
                  else if (ct == FLOAT4OID) { sv_i(&b, 24); sv_null(&b); }
                  else if (ct == FLOAT8OID) { sv_i(&b, 53); sv_null(&b); }
                  else { sv_null(&b); sv_null(&b); }
                  if (ct == TIMESTAMPOID || ct == TIMESTAMPTZOID || ct == TIMEOID ||
                      ct == INTERVALOID) sv_i(&b, 6);
                  else sv_null(&b); }
                sv_t(&b, type_name(sub->coltypes[k]));
                sv_t(&b, "NO"); sv_null(&b);
                sv_null(&b); sv_null(&b); sv_null(&b); sv_null(&b);
                sv_t(&b, "NO");
                sv_t(&b, "NEVER"); sv_null(&b);
                sv_t(&b, sysrels[i].relkind == 'v' ? "NO" : "YES");
                sv_end(&b);
            }
        }
        return r;
    }
    if (isc && !strcmp(name, "table_constraints")) {
        qr_addcol(r,"constraint_catalog",NAMEOID,-1); qr_addcol(r,"constraint_schema",NAMEOID,-1);
        qr_addcol(r,"constraint_name",NAMEOID,-1); qr_addcol(r,"table_catalog",NAMEOID,-1);
        qr_addcol(r,"table_schema",NAMEOID,-1); qr_addcol(r,"table_name",NAMEOID,-1);
        qr_addcol(r,"constraint_type",TEXTOID,-1); qr_addcol(r,"is_deferrable",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            for (int k = 0; k < t->ncons; k++) {
                Constraint *c = t->cons[k];
                const char *ty = c->contype=='p' ? "PRIMARY KEY" : c->contype=='u' ? "UNIQUE" :
                                 c->contype=='f' ? "FOREIGN KEY" : c->contype=='c' ? "CHECK" :
                                 c->contype=='n' ? "CHECK" : "EXCLUDE";
                sv_row(&b);
                sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, c->name);
                sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->name);
                sv_t(&b, ty); sv_t(&b, c->deferrable ? "YES" : "NO");
                sv_end(&b);
            }
        }
        return r;
    }
    if (isc && !strcmp(name, "key_column_usage")) {
        qr_addcol(r,"constraint_catalog",NAMEOID,-1); qr_addcol(r,"constraint_schema",NAMEOID,-1);
        qr_addcol(r,"constraint_name",NAMEOID,-1); qr_addcol(r,"table_schema",NAMEOID,-1);
        qr_addcol(r,"table_name",NAMEOID,-1); qr_addcol(r,"column_name",NAMEOID,-1);
        qr_addcol(r,"ordinal_position",INT4OID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            for (int k = 0; k < t->ncons; k++) {
                Constraint *c = t->cons[k];
                if (c->contype != 'p' && c->contype != 'u' && c->contype != 'f') continue;
                for (int q = 0; q < c->nkeys; q++) {
                    sv_row(&b);
                    sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, c->name);
                    sv_t(&b, t->schema); sv_t(&b, t->name);
                    sv_t(&b, t->cols[c->keys[q]-1].name); sv_i(&b, q + 1);
                    sv_end(&b);
                }
            }
        }
        return r;
    }
    if (isc && !strcmp(name, "views")) {
        qr_addcol(r,"table_catalog",NAMEOID,-1); qr_addcol(r,"table_schema",NAMEOID,-1);
        qr_addcol(r,"table_name",NAMEOID,-1); qr_addcol(r,"view_definition",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'v') continue;
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->name); sv_t(&b, t->viewdef);
            sv_end(&b);
        }
        return r;
    }
    if (isc && !strcmp(name, "sequences")) {
        qr_addcol(r,"sequence_catalog",NAMEOID,-1); qr_addcol(r,"sequence_schema",NAMEOID,-1);
        qr_addcol(r,"sequence_name",NAMEOID,-1); qr_addcol(r,"data_type",TEXTOID,-1);
        qr_addcol(r,"numeric_precision",INT4OID,-1);
        qr_addcol(r,"numeric_precision_radix",INT4OID,-1);
        qr_addcol(r,"numeric_scale",INT4OID,-1);
        qr_addcol(r,"start_value",TEXTOID,-1); qr_addcol(r,"minimum_value",TEXTOID,-1);
        qr_addcol(r,"maximum_value",TEXTOID,-1); qr_addcol(r,"increment",TEXTOID,-1);
        qr_addcol(r,"cycle_option",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            if (t->relkind != 'S') continue;
            Oid st = t->seqtype ? t->seqtype : INT8OID;
            sv_row(&b);
            sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->name);
            sv_t(&b, st == INT2OID ? "smallint" : st == INT4OID ? "integer" : "bigint");
            sv_i(&b, st == INT2OID ? 16 : st == INT4OID ? 32 : 64);
            sv_i(&b, 2); sv_i(&b, 0);
            char nb[32];
            snprintf(nb, sizeof nb, "%lld", (long long)t->seq_start); sv_t(&b, nb);
            snprintf(nb, sizeof nb, "%lld", (long long)t->seq_min); sv_t(&b, nb);
            snprintf(nb, sizeof nb, "%lld", (long long)t->seq_max); sv_t(&b, nb);
            snprintf(nb, sizeof nb, "%lld", (long long)t->seq_inc); sv_t(&b, nb);
            sv_t(&b, t->seq_cycle ? "YES" : "NO");
            sv_end(&b);
        }
        return r;
    }
    if (isc && !strcmp(name, "referential_constraints")) {
        qr_addcol(r,"constraint_catalog",NAMEOID,-1); qr_addcol(r,"constraint_schema",NAMEOID,-1);
        qr_addcol(r,"constraint_name",NAMEOID,-1); qr_addcol(r,"update_rule",TEXTOID,-1);
        qr_addcol(r,"delete_rule",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            for (int k = 0; k < t->ncons; k++) {
                Constraint *c = t->cons[k];
                if (c->contype != 'f') continue;
                const char *upd = c->fk_upd=='c'?"CASCADE":c->fk_upd=='r'?"RESTRICT":
                                  c->fk_upd=='n'?"SET NULL":c->fk_upd=='d'?"SET DEFAULT":"NO ACTION";
                const char *del = c->fk_del=='c'?"CASCADE":c->fk_del=='r'?"RESTRICT":
                                  c->fk_del=='n'?"SET NULL":c->fk_del=='d'?"SET DEFAULT":"NO ACTION";
                sv_row(&b);
                sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, c->name);
                sv_t(&b, upd); sv_t(&b, del);
                sv_end(&b);
            }
        }
        return r;
    }
    if (isc && !strcmp(name, "check_constraints")) {
        qr_addcol(r,"constraint_catalog",NAMEOID,-1); qr_addcol(r,"constraint_schema",NAMEOID,-1);
        qr_addcol(r,"constraint_name",NAMEOID,-1); qr_addcol(r,"check_clause",TEXTOID,-1);
        for (int i = 0; i < db->ntables; i++) {
            Table *t = db->tables[i];
            for (int k = 0; k < t->ncons; k++) {
#if PGVC_VN >= 180
                /* PostgreSQL 18 shows a not-null constraint as a check clause */
                if (t->cons[k]->contype == 'n') {
                    int an = t->cons[k]->nn_attnum;
                    sv_row(&b);
                    sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->cons[k]->name);
                    { Buf cb; buf_init(&cb);
                      buf_printf(&cb, "%s IS NOT NULL",
                                 (an >= 1 && an <= t->ncols) ? t->cols[an-1].name : "?");
                      char *x = buf_steal(&cb); sv_t(&b, x); free(x); }
                    sv_end(&b);
                }
#endif
                if (t->cons[k]->contype == 'c') {
                    sv_row(&b);
                    sv_t(&b, db->name); sv_t(&b, t->schema); sv_t(&b, t->cons[k]->name);
                    /* PostgreSQL shows the deparsed node.  Through 16 the view
                     * parenthesised the clause a second time; 17 stopped. */
                    { Buf cb; buf_init(&cb);
#if PGVC_VN >= 170
                      buf_printf(&cb, "(%s)", t->cons[k]->checksrc ? t->cons[k]->checksrc : "");
#else
                      buf_printf(&cb, "((%s))", t->cons[k]->checksrc ? t->cons[k]->checksrc : "");
#endif
                      char *x = buf_steal(&cb); sv_t(&b, x); free(x); }
                    sv_end(&b);
                }
            }
        }
        return r;
    }
    if (isc && !strcmp(name, "routines")) {
        qr_addcol(r,"specific_name",NAMEOID,-1);
        qr_addcol(r,"routine_catalog",NAMEOID,-1); qr_addcol(r,"routine_schema",NAMEOID,-1);
        qr_addcol(r,"routine_name",NAMEOID,-1); qr_addcol(r,"routine_type",TEXTOID,-1);
        qr_addcol(r,"data_type",TEXTOID,-1); qr_addcol(r,"external_language",TEXTOID,-1);
        for (int i = 0; i < db->nroutines; i++) {
            SqlRoutine *p = db->routines[i];
            char spec[128];
            snprintf(spec, sizeof spec, "%s_%u", p->name, p->oid);
            sv_row(&b);
            sv_t(&b, spec);
            sv_t(&b, db->name); sv_t(&b, p->schema); sv_t(&b, p->name);
            sv_t(&b, p->prokind == 'p' ? "PROCEDURE" : "FUNCTION");
            if (p->prokind == 'p') sv_null(&b);
            else { char *ft = format_type_str(p->rettype, -1); sv_t(&b, ft); free(ft); }
            { char up[32]; snprintf(up, sizeof up, "%s", p->lang ? p->lang : "sql");
              for (char *q = up; *q; q++) *q = (char)toupper((unsigned char)*q);
              sv_t(&b, up); }
            sv_end(&b);
        }
        return r;
    }
    if (isc && !strcmp(name, "parameters")) {
        qr_addcol(r,"specific_name",NAMEOID,-1); qr_addcol(r,"specific_catalog",NAMEOID,-1);
        qr_addcol(r,"specific_schema",NAMEOID,-1);
        qr_addcol(r,"ordinal_position",INT4OID,-1); qr_addcol(r,"parameter_mode",TEXTOID,-1);
        qr_addcol(r,"parameter_name",NAMEOID,-1); qr_addcol(r,"data_type",TEXTOID,-1);
        for (int i = 0; i < db->nroutines; i++) {
            SqlRoutine *p = db->routines[i];
            char spec[128];
            snprintf(spec, sizeof spec, "%s_%u", p->name, p->oid);
            for (int k = 0; k < p->nargs; k++) {
                char mode = p->argmodes ? p->argmodes[k] : 'i';
                sv_row(&b);
                sv_t(&b, spec); sv_t(&b, db->name); sv_t(&b, p->schema);
                sv_i(&b, k + 1);
                sv_t(&b, mode == 'o' ? "OUT" : mode == 'b' ? "INOUT" :
                         mode == 'v' ? "VARIADIC" : "IN");
                sv_t(&b, (p->argnames && p->argnames[k] && *p->argnames[k]) ? p->argnames[k] : NULL);
                { char *ft = format_type_str(p->argtypes[k], -1); sv_t(&b, ft); free(ft); }
                sv_end(&b);
            }
        }
        return r;
    }

    *found = false;
    return NULL;
}

/* ------------------------------------------------- transactional DDL undo
 * PostgreSQL rolls catalog changes back with the transaction.  Sessions here
 * share one in-memory catalog, so the undo has to be precise: each session
 * records only what it changed and reverses exactly that. */
enum { U_ADD_TABLE = 1, U_DEL_TABLE, U_ADD_INDEX, U_DEL_INDEX, U_ADD_SCHEMA,
       U_ADD_PUB, U_DEL_PUB, U_ADD_ROUTINE, U_DEL_ROUTINE, U_ADD_ROLE, U_DEL_ROLE,
       U_TABLE_STATE, U_ADD_SUB, U_DEL_SUB };

typedef struct UndoEnt {
    int kind;
    Database *db;
    void *obj;
    /* U_TABLE_STATE */
    Table *t;
    char *schema, *name, *viewdef;
    char relkind, partstrat;
    Oid parent;
    int ncols, ncons, npartkey;
    Column *cols;
    Constraint **cons;
} UndoEnt;

static __thread UndoEnt *ulog;
static __thread int nulog, culog;
static __thread bool uactive;

static void upush(UndoEnt e)
{
    if (!uactive) return;
    if (nulog == culog) { culog = culog ? culog * 2 : 32;
                          ulog = xrealloc(ulog, (size_t)culog * sizeof(UndoEnt)); }
    ulog[nulog++] = e;
}

void catalog_undo_begin(void) { nulog = 0; uactive = true; }
void catalog_undo_clear(void) { nulog = 0; uactive = false; }

void catalog_note_table_state(Database *db, Table *t)
{
    if (!uactive || !t) return;
    for (int i = 0; i < nulog; i++)
        if (ulog[i].kind == U_TABLE_STATE && ulog[i].t == t) return;
        else if (ulog[i].kind == U_ADD_TABLE && ulog[i].obj == t) return;
    UndoEnt e; memset(&e, 0, sizeof e);
    e.kind = U_TABLE_STATE; e.db = db; e.t = t;
    e.schema = t->schema; e.name = t->name; e.viewdef = t->viewdef;
    e.relkind = t->relkind; e.partstrat = t->partstrat;
    e.parent = t->parent; e.npartkey = t->npartkey;
    e.ncols = t->ncols;
    e.cols = xmalloc((size_t)(t->ncols ? t->ncols : 1) * sizeof(Column));
    memcpy(e.cols, t->cols, (size_t)t->ncols * sizeof(Column));
    e.ncons = t->ncons;
    e.cons = xmalloc((size_t)(t->ncons ? t->ncons : 1) * sizeof(Constraint *));
    memcpy(e.cons, t->cons, (size_t)t->ncons * sizeof(Constraint *));
    upush(e);
}

void catalog_note(int kind, Database *db, void *obj)
{
    UndoEnt e; memset(&e, 0, sizeof e);
    e.kind = kind; e.db = db; e.obj = obj;
    upush(e);
}

#define NOTE(k, db, o) catalog_note(k, db, o)

static void list_remove(void ***arr, int *n, void *obj)
{
    for (int i = 0; i < *n; i++)
        if ((*arr)[i] == obj) {
            memmove(&(*arr)[i], &(*arr)[i+1], (size_t)(*n - i - 1) * sizeof(void *));
            (*n)--;
            return;
        }
}
static void list_add(void ***arr, int *n, void *obj)
{
    for (int i = 0; i < *n; i++) if ((*arr)[i] == obj) return;
    *arr = xrealloc(*arr, (size_t)(*n + 1) * sizeof(void *));
    (*arr)[(*n)++] = obj;
}

void catalog_undo_apply(void)
{
    if (!uactive) return;
    for (int i = nulog - 1; i >= 0; i--) {
        UndoEnt *e = &ulog[i];
        Database *db = e->db;
        switch (e->kind) {
        case U_ADD_TABLE:   list_remove((void ***)&db->tables, &db->ntables, e->obj); break;
        case U_DEL_TABLE:   list_add((void ***)&db->tables, &db->ntables, e->obj); break;
        case U_ADD_INDEX:   list_remove((void ***)&db->indexes, &db->nindexes, e->obj); break;
        case U_DEL_INDEX:   list_add((void ***)&db->indexes, &db->nindexes, e->obj); break;
        case U_ADD_SCHEMA:  list_remove((void ***)&db->schemas, &db->nschemas, e->obj); break;
        case U_ADD_PUB:     list_remove((void ***)&db->pubs, &db->npubs, e->obj); break;
        case U_DEL_PUB:     list_add((void ***)&db->pubs, &db->npubs, e->obj); break;
        case U_ADD_SUB:     list_remove((void ***)&db->subs, &db->nsubs, e->obj); break;
        case U_DEL_SUB:     list_add((void ***)&db->subs, &db->nsubs, e->obj); break;
        case U_ADD_ROUTINE: list_remove((void ***)&db->routines, &db->nroutines, e->obj); break;
        case U_DEL_ROUTINE: list_add((void ***)&db->routines, &db->nroutines, e->obj); break;
        case U_ADD_ROLE:    list_remove((void ***)&G->roles, &G->nroles, e->obj); break;
        case U_DEL_ROLE:    list_add((void ***)&G->roles, &G->nroles, e->obj); break;
        case U_TABLE_STATE: {
            Table *t = e->t;
            t->schema = e->schema; t->name = e->name; t->viewdef = e->viewdef;
            t->relkind = e->relkind; t->partstrat = e->partstrat;
            t->parent = e->parent; t->npartkey = e->npartkey;
            if (e->ncols) {
                t->cols = xrealloc(t->cols, (size_t)e->ncols * sizeof(Column));
                memcpy(t->cols, e->cols, (size_t)e->ncols * sizeof(Column));
            }
            t->ncols = e->ncols;
            if (e->ncons) {
                t->cons = xrealloc(t->cons, (size_t)e->ncons * sizeof(Constraint *));
                memcpy(t->cons, e->cons, (size_t)e->ncons * sizeof(Constraint *));
            }
            t->ncons = e->ncons;
            break;
        }
        }
    }
    nulog = 0;
    uactive = false;
}

Datum textarr_pub(Session *s, char **v, int n) { return textarr(s, v, n); }
/* ------------------------------------------------- pg_get_constraintdef */
char *constraint_def_text(Session *s, Database *db, Table *t, Constraint *c)
{
    Buf b; buf_init(&b);
    switch (c->contype) {
    case 'c':
        buf_printf(&b, "CHECK ((%s))", c->checksrc ? c->checksrc : "");
        break;
    case 'n':
        buf_printf(&b, "NOT NULL %s",
                   (c->nn_attnum >= 1 && c->nn_attnum <= t->ncols)
                   ? t->cols[c->nn_attnum-1].name : "?");
        break;
    case 'p': case 'u': case 'x': {
        buf_adds(&b, c->contype == 'p' ? "PRIMARY KEY (" :
                     c->contype == 'u' ? "UNIQUE (" : "EXCLUDE (");
        for (int i = 0; i < c->nkeys; i++) {
            if (i) buf_adds(&b, ", ");
            int an = c->keys[i];
            buf_adds(&b, (an >= 1 && an <= t->ncols) ? t->cols[an-1].name : "?");
            if (c->without_overlaps && i == c->nkeys - 1) buf_adds(&b, " WITHOUT OVERLAPS");
        }
        buf_addc(&b, ')');
        break;
    }
    case 'f': {
        buf_adds(&b, "FOREIGN KEY (");
        for (int i = 0; i < c->nkeys; i++) {
            if (i) buf_adds(&b, ", ");
            int an = c->keys[i];
            if (c->fk_period && i == c->nkeys - 1) buf_adds(&b, "PERIOD ");
            buf_adds(&b, (an >= 1 && an <= t->ncols) ? t->cols[an-1].name : "?");
        }
        buf_adds(&b, ") REFERENCES ");
        Table *ft = tbl_find_oid(db, c->fk_reltable);
        buf_adds(&b, ft ? ft->name : "?");
        buf_addc(&b, '(');
        for (int i = 0; i < c->nfkeys; i++) {
            if (i) buf_adds(&b, ", ");
            int an = c->fkeys[i];
            if (c->fk_period && i == c->nfkeys - 1) buf_adds(&b, "PERIOD ");
            buf_adds(&b, (ft && an >= 1 && an <= ft->ncols) ? ft->cols[an-1].name : "?");
        }
        buf_addc(&b, ')');
        const char *ua = c->fk_upd == 'r' ? " ON UPDATE RESTRICT" :
                         c->fk_upd == 'c' ? " ON UPDATE CASCADE" :
                         c->fk_upd == 'n' ? " ON UPDATE SET NULL" :
                         c->fk_upd == 'd' ? " ON UPDATE SET DEFAULT" : "";
        const char *da = c->fk_del == 'r' ? " ON DELETE RESTRICT" :
                         c->fk_del == 'c' ? " ON DELETE CASCADE" :
                         c->fk_del == 'n' ? " ON DELETE SET NULL" :
                         c->fk_del == 'd' ? " ON DELETE SET DEFAULT" : "";
        buf_adds(&b, ua); buf_adds(&b, da);
        break;
    }
    default: break;
    }
    if (c->deferrable) buf_adds(&b, " DEFERRABLE");
#if PGVC_VN >= 180
    if (c->not_enforced) buf_adds(&b, " NOT ENFORCED");
    else if (c->not_valid) buf_adds(&b, " NOT VALID");
#else
    if (c->not_valid) buf_adds(&b, " NOT VALID");
#endif
    (void)s;
    return buf_steal(&b);
}

char *constraint_def_by_oid(Session *s, Oid oid)
{
    Database *db = s ? s->db : NULL;
    if (!db) return NULL;
    for (int i = 0; i < db->ntables; i++) {
        Table *t = db->tables[i];
        for (int k = 0; k < t->ncons; k++)
            if (t->cons[k]->oid == oid) return constraint_def_text(s, db, t, t->cons[k]);
    }
    return NULL;
}

/* --------------------------------------------------------------- domains
 * A domain is a base type plus an optional CHECK; PostgreSQL gives it its own
 * type oid, so the parser, format_type() and the input function all see it. */
typedef struct DomainDef {
    Oid   oid;
    char *name;
    Oid   basetype; int32 typmod;
    char *checksrc; char *conname;
    bool  notnull;
} DomainDef;
static DomainDef *domains;
static int ndomains;

Oid domain_create(Session *s, const char *name, Oid base, int32 typmod,
                  const char *checksrc, bool notnull)
{
    for (int i = 0; i < ndomains; i++)
        if (!strcmp(domains[i].name, name))
            ereport(s, "42710", "type \"%s\" already exists", name);
    domains = xrealloc(domains, (size_t)(ndomains + 1) * sizeof(DomainDef));
    DomainDef *d = &domains[ndomains++];
    memset(d, 0, sizeof *d);
    d->oid = new_oid();
    d->name = xstrdup(name);
    d->basetype = base; d->typmod = typmod;
    d->checksrc = checksrc ? xstrdup(checksrc) : NULL;
    d->notnull = notnull;
    Buf b; buf_init(&b);
    buf_printf(&b, "%s_check", name);
    d->conname = buf_steal(&b);
    return d->oid;
}

bool domain_drop(const char *name)
{
    for (int i = 0; i < ndomains; i++)
        if (!strcmp(domains[i].name, name)) {
            memmove(&domains[i], &domains[i+1],
                    (size_t)(ndomains - i - 1) * sizeof(DomainDef));
            ndomains--;
            return true;
        }
    return false;
}

Oid domain_oid_by_name(const char *name)
{
    for (int i = 0; i < ndomains; i++)
        if (!strcmp(domains[i].name, name)) return domains[i].oid;
    return 0;
}

const char *domain_name_of(Oid oid)
{
    for (int i = 0; i < ndomains; i++)
        if (domains[i].oid == oid) return domains[i].name;
    return NULL;
}

/* The type a domain is built on, following a chain of domains to its end. */
Oid domain_basetype(Oid oid, int32 *typmod)
{
    for (int guard = 0; guard < 16; guard++) {
        int found = -1;
        for (int i = 0; i < ndomains; i++) if (domains[i].oid == oid) found = i;
        if (found < 0) break;
        if (typmod && domains[found].typmod >= 0) *typmod = domains[found].typmod;
        oid = domains[found].basetype;
    }
    return oid;
}

/* One step of the domain chain, for the executor's constraint check. */
bool domain_step(Oid oid, Oid *base, const char **checksrc, const char **conname,
                 const char **dname, bool *notnull)
{
    for (int i = 0; i < ndomains; i++)
        if (domains[i].oid == oid) {
            *base = domains[i].basetype;
            *checksrc = domains[i].checksrc;
            *conname = domains[i].conname;
            *dname = domains[i].name;
            *notnull = domains[i].notnull;
            return true;
        }
    return false;
}
