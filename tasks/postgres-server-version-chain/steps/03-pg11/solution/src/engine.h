/* engine.h - catalog, values, parse tree and executor interfaces. */
#ifndef PGVC_ENGINE_H
#define PGVC_ENGINE_H

#include "pgvc.h"
#include <setjmp.h>
#include <pthread.h>

/* ====================================================================== values */
typedef struct ArrayVal ArrayVal;
typedef struct RangeVal RangeVal;
typedef struct MRangeVal MRangeVal;
typedef struct RecordVal RecordVal;

typedef struct Datum {
    Oid   type;
    bool  isnull;
    char *regname;                /* display name for regclass/regtype/regproc */
    union {
        int64      i;          /* bool int2 int4 int8 oid date time timestamp(tz) */
        double     f;          /* float4 float8 */
        Numeric   *num;
        struct { char *p; int32 len; } s;   /* text varchar bpchar name bytea uuid json jsonpath */
        JsonVal   *jb;         /* jsonb */
        PgInterval iv;
        ArrayVal  *arr;
        RangeVal  *rng;
        MRangeVal *mrng;
        RecordVal *rec;
    } v;
} Datum;

struct ArrayVal {                 /* multi-dimensional, PostgreSQL semantics */
    Oid   elemtype;
    int   ndim;
    int   dims[6];
    int   lbound[6];
    int   nelem;
    Datum *elems;
};

struct RangeVal {
    Oid   rngtype;                /* the range type oid */
    Oid   subtype;
    bool  empty;
    bool  lower_inf, upper_inf;
    bool  lower_inc, upper_inc;
    Datum lower, upper;
};

struct MRangeVal {
    Oid    mrngtype;
    Oid    rngtype;
    int    n;
    RangeVal **ranges;
};

struct RecordVal { int n; Datum *vals; char **names; };

Datum d_null(Oid type);
Datum d_bool(bool b);
Datum d_int(Oid type, int64 v);
Datum d_float(Oid type, double v);
Datum d_num(Numeric *n);
Datum d_text(Oid type, const char *s);
Datum d_textn(Oid type, const char *s, int32 len);
Datum d_copy(Datum d);
void  d_free(Datum d);

/* ---------------------------------------------------------------- tsearch.c */
bool  ts_config_known(const char *cfg);
char *ts_to_tsvector(const char *cfg, const char *text);
char *ts_to_tsvector_parts(const char *cfg, char **parts, int nparts);
char *ts_websearch_to_tsquery(const char *cfg, const char *q, int *empty_reason);
bool  ts_match(const char *vector, const char *query);

/* ================================================================== error path */
#define ERRBUF 1024
typedef struct ErrState {
    char sqlstate[6];
    char message[ERRBUF];
    char detail[ERRBUF];
    char hint[ERRBUF];
    char context[ERRBUF];
    int  position;                /* 1-based character position, 0 = none */
} ErrState;

typedef struct Session Session;

void ereport(Session *s, const char *sqlstate, const char *fmt, ...)
     __attribute__((format(printf,3,4), noreturn));
void ereport_pos(Session *s, const char *sqlstate, int pos, const char *fmt, ...)
     __attribute__((format(printf,4,5), noreturn));
void err_detail(Session *s, const char *fmt, ...) __attribute__((format(printf,2,3)));
void err_hint(Session *s, const char *fmt, ...) __attribute__((format(printf,2,3)));
void err_context(Session *s, const char *fmt, ...) __attribute__((format(printf,2,3)));
void warning_hint(Session *s, const char *sqlstate, const char *hint, const char *fmt, ...)
     __attribute__((format(printf,4,5)));
void notice(Session *s, const char *sqlstate, const char *fmt, ...)
     __attribute__((format(printf,3,4)));
void warning(Session *s, const char *sqlstate, const char *fmt, ...)
     __attribute__((format(printf,3,4)));
void warning_hint(Session *s, const char *sqlstate, const char *hint, const char *fmt, ...)
     __attribute__((format(printf,4,5)));
void warning_detail_hint(Session *s, const char *sqlstate, const char *detail,
                         const char *hint, const char *fmt, ...)
     __attribute__((format(printf,5,6)));

/* ===================================================================== catalog */
typedef struct Column {
    char *name;
    Oid   type;
    int32 typmod;
    bool  notnull;
    char *defexpr;            /* source text of DEFAULT */
    char  generated;          /* 0 | 's' stored | 'v' virtual */
    char *genexpr;
    char  identity;           /* 0 | 'a' ALWAYS | 'd' BY DEFAULT */
    char *identity_seq;       /* sequence name */
    int   attnum;
    bool  dropped;
    char *collation;
    char *missingval;         /* PostgreSQL 11 "fast default" for existing rows */
    char  compression;        /* 0 default, 'p' pglz, 'l' lz4 */
} Column;

typedef struct Constraint {
    Oid   oid;                /* assigned lazily by the pg_constraint scan */
    char  contype;            /* p primary, u unique, c check, f foreign, n not-null, x exclusion */
    char *name;
    int  *keys; int nkeys;     /* attnums */
    bool  without_overlaps;    /* PostgreSQL 18 temporal PK/UNIQUE */
    bool  nulls_nd;            /* NULLS NOT DISTINCT (PostgreSQL 15) */
    char *checksrc;
    Oid   fk_reltable;         /* referenced table oid */
    int  *fkeys; int nfkeys;
    char  fk_upd, fk_del;      /* a no action, r restrict, c cascade, n set null, d set default */
    bool  fk_period;
    int   nn_attnum;           /* for contype 'n' */
    bool  deferrable;
    bool  not_valid;           /* declared NOT VALID and not yet validated */
    bool  not_enforced;        /* PostgreSQL 18 NOT ENFORCED */
    bool  noinherit;
    int   inhcount;            /* how many parents this constraint came from */
    bool  islocal;
} Constraint;

typedef struct Row {
    uint64 xmin, xmax;         /* MVCC */
    uint64 locker;             /* transaction holding a row lock, 0 = free */
    int    n;
    Datum *vals;
    struct Row *next_ver;
} Row;

typedef struct Index {
    Oid   oid;
    char *name;
    Oid   table;
    int  *keys; int nkeys;
    int  *incl; int nincl;     /* INCLUDE columns (PostgreSQL 11) */
    bool  unique, primary;
    char *predicate;
    char **exprs;              /* expression keys, parallel to keys (NULL entries) */
    bool  partitioned;
    Oid   am;                  /* access method oid: 403 btree, 405 hash, ... */
    Oid   parent_index;        /* partitioned index this one was cloned from */
    bool  valid;
    char  conkind;             /* 0 plain, 'p' primary key, 'u' unique constraint */
    int  *stattarget; bool statset;   /* ALTER INDEX ... SET STATISTICS */
    bool  nulls_nd;            /* NULLS NOT DISTINCT (PostgreSQL 15) */
    char **optname, **optval; int nopts;   /* reloptions, in the order they were set */
} Index;

/* Extended statistics object (PostgreSQL 10). */
typedef struct ExtStat {
    Oid   oid;
    char *schema, *name;
    Oid   table;
    int  *keys; int nkeys;
    char *kinds;               /* subset of "dfme", sorted the way PostgreSQL stores it */
    char **exprs; int nexprs;  /* expression keys (PostgreSQL 14) */
    int   stattarget;          /* -1 = the default */
} ExtStat;

typedef struct Table {
    Oid   oid;
    char *schema;
    char *name;
    char  relkind;             /* r v i S p I m */
    Column *cols; int ncols;
    Constraint **cons; int ncons;
    Row  **rows; int nrows, caprows;
    char *viewdef;
    int   check_option;        /* 0 none, 1 LOCAL, 2 CASCADED */
    bool  security_invoker;
    char **optname, **optval; int nopts;   /* reloptions, in order */
    char  partstrat;           /* 0 r l h */
    int  *partkey; int npartkey;
    char **partkeyexpr;
    Oid   parent;              /* partition parent, 0 if none */
    char *partbound;           /* FOR VALUES ... text */
    /* Partition bound.  fromkind/tokind mark a bound element as MINVALUE (-1),
     * finite (0) or MAXVALUE (+1); PostgreSQL needs the three apart because a
     * range bound is compared column by column against the whole key tuple. */
    struct { bool is_default; bool is_hash; int modulus, remainder;
             Datum *from, *to; int nfrom, nto;
             signed char *fromkind, *tokind;
             Datum *in; int nin; } pbound;
    /* sequence state */
    int64 seq_last, seq_min, seq_max, seq_inc, seq_start, seq_cache;
    bool  seq_called, seq_cycle;
    Oid   seqtype;             /* AS smallint|integer|bigint */
    Oid   seq_owner;           /* table that owns this sequence, 0 if standalone */
    Oid   am;                  /* table access method (PostgreSQL 12) */
    char *owner;
    char *acl;                 /* privileges granted to PUBLIC */
    char **grantee, **gacl; int ngrants;
    pthread_mutex_t lock;
    uint64 lock_holder;        /* xid holding a write lock on the table, 0 = free */
} Table;

typedef struct Publication {
    Oid oid; char *name;
    bool alltables, pub_insert, pub_update, pub_delete, pub_truncate, viaroot;
    Oid *tables; int ntables;
    char **where; char ***collist; int *ncollist;
} Publication;

/* A subscription as CREATE SUBSCRIPTION records it.  This server has no apply
 * worker, so a subscription is only ever a catalog entry: the fields exist
 * because pg_subscription exposes them, not because anything replicates. */
typedef struct Subscription {
    Oid oid; char *name;
    bool enabled, binary, disableonerr, passwordrequired, runasowner, failover;
    char stream;               /* 'f' off, 't' on, 'p' parallel */
    char twophase;             /* 'd' disabled, 'p' pending, 'e' enabled */
    char *conninfo, *slotname, *synccommit, *origin;
    char **pubs; int npubs;
} Subscription;

typedef struct RoleEntry {
    Oid oid; char *name;
    bool canlogin, superuser, createdb, createrole, replication, bypassrls, inherit;
    char *password;            /* verifier text */
} RoleEntry;

typedef struct SqlRoutine {
    Oid oid; char *schema; char *name;
    char prokind;              /* f function, p procedure */
    Oid *argtypes; char **argnames; int nargs;
    char *argmodes;            /* per argument: 0 IN, 1 OUT, 2 INOUT */
    Oid rettype;
    char *body;
    char *lang;
    bool retset;
    char volatility;           /* i s v */
} SqlRoutine;

/* A trigger, as CREATE TRIGGER defines it. */
typedef struct Trigger {
    Oid   oid;
    char *name;
    Oid   table;
    int   timing;              /* 0 BEFORE, 1 AFTER, 2 INSTEAD OF */
    int   events;              /* 1 INSERT | 2 UPDATE | 4 DELETE | 8 TRUNCATE */
    bool  forrow;
    char *funcname;
    char *whensrc;
    char *oldtable, *newtable; /* REFERENCING transition table names */
    bool  isclone;             /* cloned onto a partition from its parent */
    Oid   parent_trig;
} Trigger;

/* What a PL/pgSQL trigger function sees. */
typedef struct PlTrigCtx {
    const char *name, *when, *level, *op;
    Table *table;
    Datum *oldrow, *newrow;
} PlTrigCtx;

Datum plpgsql_run(Session *s, SqlRoutine *p, Datum *args, int nargs,
                  PlTrigCtx *trig, Datum **outvals, int *noutvals);
void  plpgsql_do(Session *s, const char *body);

typedef struct Schema { Oid oid; char *name; char *owner; char *acl;
                        char **grantee, **gacl; int ngrants; } Schema;

typedef struct Database {
    char *name;
    Schema **schemas; int nschemas;
    Table **tables; int ntables;
    Index **indexes; int nindexes;
    Publication **pubs; int npubs;
    Subscription **subs; int nsubs;
    SqlRoutine **routines; int nroutines;
    ExtStat **stats; int nstats;
    Trigger **triggers; int ntriggers;
    Oid nextoid;
} Database;

typedef struct Cluster {
    Database **dbs; int ndbs;
    RoleEntry **roles; int nroles;
    char *datadir;
    pthread_rwlock_t big;      /* DDL exclusion */
    pthread_mutex_t  mux;      /* catalog mutation */
    uint64 next_xid;
    Oid    next_oid;
    char  *guc_defaults[64];
} Cluster;

/* Declared here because more than one translation unit calls them; an implicit
 * declaration would give the caller a different prototype from the definition. */
Oid  multirange_of_range(Oid t);
void tbl_add(Database *db, Table *t);
void tbl_drop(Database *db, Table *t);
void catalog_note(int kind, Database *db, void *obj);
void catalog_note_table_state(Database *db, Table *t);


extern Cluster *G;

Database *db_find(const char *name);
Database *db_create(const char *name);
Table *tbl_find(Database *db, const char *schema, const char *name);
Table *tbl_find_sp(Session *s, const char *schema, const char *name);
Table *tbl_find_oid(Database *db, Oid oid);
Schema *sch_find(Database *db, const char *name);
/* privileges: an ACL is the letter string PostgreSQL uses in aclitem text */
const char *session_role(Session *s);
bool  acl_has(const char *acl, char p);
void  acl_apply(char **aclp, const char *privs, bool grant, bool isschema);
bool  schema_priv(Session *s, Schema *sc, const char *role, char p);
void  schema_grant(Schema *sc, const char *role, const char *privs, bool grant);
bool  table_priv(Session *s, Table *t, const char *role, char p);
void  table_grant(Table *t, const char *role, const char *privs, bool grant);
Index *idx_find(Database *db, const char *name);
int    col_index(Table *t, const char *name);
RoleEntry *role_find(const char *name);
RoleEntry *role_by_oid(Oid oid);

/* ===================================================================== session */
#define MAXGUC 64
typedef struct GucVal { char *name; char *val; } GucVal;

typedef struct PreparedStmt {
    char *name;
    char *sql;
    struct Node *tree;
    Oid  *paramtypes; int nparams;
} PreparedStmt;

typedef struct Portal {
    char *name;
    PreparedStmt *stmt;
    Datum *params; int nparams;
    int16 *resfmt; int nresfmt;
    struct QueryResult *res;
    int    next_row;
    bool   done;
    bool   is_cursor;          /* created by DECLARE, so Execute does not consume it */
} Portal;

typedef struct QueryResult {
    int ncols;
    char **colnames;
    Oid  *coltypes;
    int32 *coltypmods;
    int16 *colfmts;
    int   nrows, caprows;
    Datum **rows;
    char  tag[64];
    bool  has_rowdesc;
} QueryResult;

struct Session {
    int    fd;
    int    oppos;             /* position of the operator being applied */
    int    castpos;           /* position of the :: of an explicit cast */
    int    jsonpath_pos;      /* position of the jsonpath literal in the query */
    int    funcargpos;        /* position of the first argument of the call */
    char  *role;              /* SET ROLE, when it is not the login role */
    const char *eff_role;     /* owner of the view whose body is running */
    int    argpos[16];        /* position of each argument of the call */
    Database *db;
    char  *user;
    char  *dbname;
    char  *appname;
    int    proto_major, proto_minor;
    uint32 pid, secret;
    /* transaction */
    int    xact_state;          /* 0 idle, 1 in transaction, 2 failed */
    uint64 xid;
    int    isolation;           /* 0 read committed, 1 repeatable read, 2 serializable */
    bool   readonly;
    /* GUCs */
    GucVal gucs[MAXGUC]; int nguc;
    /* values SET LOCAL replaced, restored when the transaction ends */
    GucVal gucsave[MAXGUC]; int ngucsave;
    /* prepared statements and portals */
    PreparedStmt *stmts[64]; int nstmts;
    Portal *portals[64]; int nportals;

    /* error handling */
    jmp_buf jmp;
    bool    in_try;
    ErrState err;
    /* output */
    Buf out;
    bool  copy_in;
    bool  ignore_till_sync;
    /* replication connection */
    bool  replication;
    /* set by a CancelRequest arriving on another connection */
    volatile int cancel_pending;
    /* scratch */
    void *cur;
};

const char *guc_get(Session *s, const char *name);
void        guc_set(Session *s, const char *name, const char *val);
void        guc_set_local(Session *s, const char *name, const char *val);
void        guc_end_xact(Session *s);
bool        guc_known(const char *name);
const char *guc_default(const char *name);

/* ======================================================================= parse */
typedef enum {
    N_SELECT=1, N_INSERT, N_UPDATE, N_DELETE, N_MERGE,
    N_CREATE_TABLE, N_CREATE_INDEX, N_CREATE_VIEW, N_CREATE_SCHEMA, N_CREATE_SEQ,
    N_CREATE_ROLE, N_CREATE_PUB, N_CREATE_FUNC, N_CREATE_TYPE,
    N_DROP, N_ALTER_TABLE, N_ALTER_SEQ, N_ALTER_ROLE, N_ALTER_PUB, N_ALTER_SYSTEM,
    N_ALTER_SUB,
    N_TRANSACTION, N_SET, N_SHOW, N_RESET, N_COPY, N_EXPLAIN, N_PREPARE, N_EXECUTE,
    N_DEALLOCATE, N_CALL, N_VALUES, N_GRANT, N_TRUNCATE, N_ANALYZE, N_VACUUM,
    N_REINDEX, N_CHECKPOINT, N_COMMENT, N_DISCARD, N_LOCK, N_CLUSTER, N_DO,
    N_CREATE_DB, N_DROP_DB, N_EMPTY, N_LISTEN, N_NOTIFY, N_UNLISTEN, N_REFRESH,
    N_CREATE_TRIGGER, N_SECURITY_LABEL, N_CREATE_SUB, N_START_REPL, N_IDENT_SYS,
    N_CREATE_REPL_SLOT, N_DROP_REPL_SLOT, N_BASE_BACKUP, N_TIMELINE_HISTORY,
    N_SHOW_REPL, N_READ_REPL_SLOT, N_UPLOAD_MANIFEST,
    N_DECLARE, N_FETCH, N_CLOSE_CURSOR, N_CREATE_STATS, N_CREATE_DOMAIN
} NodeTag;

typedef struct Node Node;
typedef struct Expr Expr;

typedef enum {
    X_CONST=1, X_PARAM, X_COLREF, X_FUNC, X_OP, X_AND, X_OR, X_NOT, X_CASE,
    X_CAST, X_SUBLINK, X_LIST, X_STAR, X_AGG, X_WINDOW, X_COALESCE, X_NULLIF,
    X_ARRAY, X_ROW, X_SUBSCRIPT, X_BETWEEN, X_IN, X_IS, X_LIKE, X_EXISTS,
    X_ANYALL, X_COLLATE, X_GROUPING, X_TYPECAST_STR, X_JSONCTOR, X_ISJSON,
    X_MERGEACTION, X_OLDNEW
} ExprTag;

struct Expr {
    ExprTag tag;
    int     pos;               /* 1-based char position in the query text */
    /* X_CONST */
    Datum   val;
    char   *strval;            /* unresolved literal text */
    bool    isnull_const;
    /* X_PARAM */
    int     paramno;
    int     basepos;          /* start of a subscripted expression */
    /* X_COLREF */
    char   *qual;              /* table alias or NULL */
    char   *colname;
    char   *qual2;             /* schema.table.col */
    /* X_FUNC / X_OP / X_AGG / X_WINDOW */
    char   *name;
    char   *nspname;
    Expr  **args; int nargs;
    char  **argnames; int nargnames;   /* named arguments, in call order */
    bool    agg_distinct;
    bool    agg_star;
    bool    grammar_func;     /* spelled in the grammar: errors say pg_catalog.f */
    struct SortItem *agg_order; int agg_norder;
    Expr   *filter;
    struct WindowDef *over;
    /* X_CASE */
    Expr   *case_arg;
    Expr  **when_cond; Expr **when_res; int nwhen;
    Expr   *case_else;
    /* X_CAST */
    Oid     casttype; int32 casttypmod; char *casttypename;
    /* X_SUBLINK / X_EXISTS / X_IN / X_ANYALL */
    Node   *subquery;
    int     sublink_kind;      /* 0 scalar, 1 exists, 2 in, 3 any, 4 all, 5 array */
    Expr   *testexpr;
    /* X_IS */
    int     is_kind;           /* 0 null,1 not null,2 true,3 not true,4 false,5 not false,
                                  6 unknown,7 not unknown,8 distinct,9 not distinct */
    /* X_LIKE */
    bool    like_not, like_ilike, like_similar;
    Expr   *like_escape;
    /* X_BETWEEN */
    bool    between_not, between_sym;
    /* X_ISJSON */
    int     isjson_kind;       /* 0 value 1 array 2 object 3 scalar */
    bool    isjson_not, isjson_unique;
    /* X_JSONCTOR */
    int     json_ctor;         /* 0 JSON_ARRAY 1 JSON_OBJECT 2 JSON_ARRAYAGG 3 JSON_OBJECTAGG
                                  4 JSON 5 JSON_SCALAR 6 JSON_SERIALIZE 7 JSON_QUERY
                                  8 JSON_VALUE 9 JSON_EXISTS */
    bool    json_absent_on_null;
    bool    json_jsonb_default;   /* a jsonb argument makes jsonb the default */
    struct JsonBehav *json_behav; /* JSON_QUERY / JSON_VALUE / JSON_EXISTS options */
    /* X_OLDNEW */
    int     oldnew;            /* 1 OLD, 2 NEW */
    char   *alias;             /* AS label */
};

/* The ON EMPTY / ON ERROR and wrapper options of a SQL/JSON query function. */
typedef struct JsonBehav {
    int    wrapper;          /* 0 without, 1 unconditional, 2 conditional */
    int    quotes;           /* 0 keep (default), 1 omit */
    int    on_empty, on_error;   /* 0 null, 1 error, 2 default expr, 3 empty array,
                                    4 empty object, 5 true, 6 false, 7 unknown */
    Expr  *empty_default, *error_default;
    Expr **passing; char **passnames; int npassing;
} JsonBehav;

typedef struct SortItem { Expr *e; int dir; int nulls; } SortItem;  /* dir 0 asc 1 desc; nulls 0 dflt 1 first 2 last */

typedef struct WindowDef {
    char *name, *refname;
    Expr **partition; int npartition;
    SortItem *order; int norder;
    int   frame_mode;          /* 0 range 1 rows 2 groups */
    int   frame_start, frame_end;   /* 0 unbounded preceding,1 offset preceding,2 current row,
                                        3 offset following,4 unbounded following, -1 unset */
    Expr *start_off, *end_off;
    int   frame_excl;
    int   pos;                 /* char position of the window definition's "(" */
    int   start_kw_pos;        /* char position of the start bound's keyword */
} WindowDef;


/* ------------------------------------------------------------- time zones */
/* tzdata.c: the offsets the operating system's IANA zone files describe. */
bool tz_known(const char *zone);
bool tz_offset_at(const char *zone, int64 utc, int *gmtoff,
                  char *abbr, int abbrlen, bool *isdst);
bool tz_local_to_utc(const char *zone, int64 local, int64 *utc);
/* -------------------------------------------------------------- XMLTABLE */
typedef struct XmlNode XmlNode;
XmlNode *xml_parse_doc(const char *s, char **err);
char *xml_node_text(XmlNode *n);
bool xml_path(XmlNode *root, XmlNode *ctx, const char *path,
              XmlNode ***out, int *nout);

typedef struct XmlTableCol {
    char *name;
    Oid   type; int32 typmod;
    char *path;                 /* used when no PATH clause was given */
    struct Expr *pathexpr;
    struct Expr *defaultexpr;
    bool  forordinality, notnull;
} XmlTableCol;

typedef struct XmlTableDef {
    struct Expr *rowexpr;
    struct Expr *docexpr;
    XmlTableCol *cols; int ncols;
} XmlTableDef;

/* JSON_TABLE, added in PostgreSQL 17. */
typedef struct JsonTableCol {
    char  *name;
    int    kind;              /* 0 regular, 1 ordinality, 2 exists, 3 nested */
    Oid    type; int32 typmod; char *typname;
    char  *path; int pathpos;
    bool   format_json;
    int    wrapper, quotes;
    int    on_empty, on_error;
    Expr  *empty_default, *error_default;
    struct JsonTableCol **cols; int ncols;
    int    namepos;
} JsonTableCol;

typedef struct JsonTableDef {
    Expr  *ctx, *path; int pathpos;
    Expr **passing; char **passnames; int npassing;
    JsonTableCol **cols; int ncols;
    int    on_error;          /* 0 empty (default), 1 error */
} JsonTableDef;

typedef struct RangeTbl {
    int   pos;
    int   kind;                /* 0 relation, 1 subquery, 2 join, 3 function, 4 values, 5 cte */
    char *schema, *relname, *alias;
    char **colaliases; int ncolaliases;
    Node *subquery;
    Expr *funcexpr;
    bool  ordinality;          /* WITH ORDINALITY on a function scan */
    JsonTableDef *jtable;
    bool  lateral;
    bool  only;
    /* join */
    int   jointype;            /* 0 inner 1 left 2 right 3 full 4 cross */
    struct RangeTbl *left, *right;
    Expr *joinqual;
    char **usingcols; int nusing;
    bool  natural;
    /* values */
    Expr ***values; int nvalrows, nvalcols;
    /* XMLTABLE */
    XmlTableDef *xmlt;
} RangeTbl;

typedef struct CteDef { char *name; char **cols; int ncols; Node *query;
                        bool recursive; int materialized; /* -1 never,0 auto,1 always */
                        Expr *search_col; int search_kind; SortItem *search_by; int nsearch;
                        char *cycle_col, *cycle_path; Expr **cycle_cols; int ncycle;
                        Datum cycle_mark, cycle_default; bool has_cycle;
                        Expr *cycle_mark_e, *cycle_default_e;
                        int name_pos, search_pos, cycle_pos; } CteDef;

typedef struct SelectStmt {
    CteDef **ctes; int nctes; bool recursive;
    Expr **target; int ntarget;
    bool   distinct; Expr **distinct_on; int ndistinct_on;
    RangeTbl **from; int nfrom;
    Expr  *where;
    Expr **groupby; int ngroupby;
    bool   group_distinct;
    Expr  *having;
    WindowDef **windows; int nwindows;
    SortItem *orderby; int norderby;
    Expr  *limit, *offset;
    bool   with_ties;
    int    with_ties_pos;      /* char position of the WITH keyword */
    bool   fetch_first;        /* spelled FETCH FIRST rather than LIMIT */
    int    setop;              /* 0 none, 1 union, 2 intersect, 3 except */
    bool   setop_all;
    struct SelectStmt *larg, *rarg;
    Expr ***values; int nvalrows, nvalcols;   /* VALUES list */
    int    lock_kind;          /* 0 none 1 update 2 share 3 no key update 4 key share */
    bool   lock_nowait, lock_skip;
} SelectStmt;

struct Node {
    NodeTag tag;
    char   *sql;                /* original text */
    /* statements keep their payload in a union-ish set of fields */
    SelectStmt *select;
    void   *x;                  /* statement-specific struct */
};

Node *parse_sql(Session *s, const char *sql, const char **endptr);
Expr *parse_expr_string(Session *s, const char *text);

/* ==================================================================== executor */
QueryResult *qr_new(void);
void qr_free(QueryResult *r);
void qr_addcol(QueryResult *r, const char *name, Oid type, int32 typmod);
void qr_addrow(QueryResult *r, Datum *vals);

QueryResult *exec_node(Session *s, Node *n);
QueryResult *exec_sql(Session *s, const char *sql);

/* value <-> text */
char  *out_func(Session *s, Datum d);
Datum  reg_make(Session *s, Oid regtype, int64 oid, const char *name);
Datum  in_func(Session *s, Oid type, int32 typmod, const char *str, int len);
char  *snapshot_canonical(const char *str);
bool   snapshot_parts(const char *canon, uint64 *xmin, uint64 *xmax,
                      uint64 *xip, int maxxip, int *nxip);
Datum  coerce_to(Session *s, Datum d, Oid type, int32 typmod, int ctx); /* ctx 0 implicit 1 assign 2 explicit */
int    d_cmp(Session *s, Datum a, Datum b);
bool   d_eq(Session *s, Datum a, Datum b);
const char *type_name(Oid t);
Oid    type_by_name(const char *name);
Oid    array_type_of(Oid elem);
Oid    elem_type_of(Oid arr);
bool   type_is_array(Oid t);
bool   type_is_range_like(Oid t);
bool   type_is_multirange_like(Oid t);
int32  parse_typmod(Session *s, Oid t, Expr **args, int nargs);
char  *format_type_str(Oid t, int32 typmod);

/* function dispatch */
void  check_cancel(Session *s);
Datum eval_expr(Session *s, Expr *e, void *ctx);
Datum call_builtin(Session *s, const char *name, Datum *args, int nargs, bool *found);

/* protocol callbacks used by the executor */
void proto_copy_out_row(Session *s, const char *line, size_t n);
bool proto_copy_in_line(Session *s, char **line, size_t *n);

/* misc */
typedef struct ProcDef { const char *name; char kind; int minvn, maxvn;
                         const char *args; const char *result;
                         short nargs1; Oid rettype; Oid arg0; } ProcDef;
const ProcDef *builtin_procs(int *n);

bool parse_type_from_text(Session *s, const char *txt, Oid *oid, int32 *typmod,
                          char **canonical);

Oid new_oid(void);

/* domains (catalog.c) */
Oid   domain_create(Session *s, const char *name, Oid base, int32 typmod,
                    const char *checksrc, bool notnull);
bool  domain_drop(const char *name);
Oid   domain_oid_by_name(const char *name);
const char *domain_name_of(Oid oid);
Oid   domain_basetype(Oid oid, int32 *typmod);
void  domain_check(Session *s, Oid oid, Datum v);
bool  domain_step(Oid oid, Oid *base, const char **checksrc, const char **conname,
                  const char **dname, bool *notnull);

bool json_keys_unique(const JsonVal *v);
Datum eval_json_ctor(Session *s, Expr *e, Datum *args, int nargs);
Datum eval_json_query_fn(Session *s, Expr *e, Datum ctxitem, Datum pathd,
                         Datum (*evalx)(void *, Expr *), void *ctx);
char *jsonpath_leftover(Session *s, const char *path);
int   json_table_run(Session *s, JsonTableDef *jt, Datum ctxd, Datum pathd,
                     Datum (*evalx)(void *, Expr *), void *ctx,
                     JsonTableCol ***colsout, int *ncolsout, Datum ***rowsout);
Datum eval_json_agg(Session *s, Expr *e, Datum *vals, Datum *keys, int nv);
void  json_quote_into(Buf *b, const char *p, size_t n);
void  json_value_into(Session *s, Buf *b, Datum d);
void  json_key_into(Session *s, Buf *b, Datum d);
Datum json_ctor_finish(Session *s, Expr *e, Buf *b);

/* ------------------------------------------------------------- time zones */
/* tzdata.c: the offsets the operating system's IANA zone files describe. */
bool tz_known(const char *zone);
bool tz_offset_at(const char *zone, int64 utc, int *gmtoff,
                  char *abbr, int abbrlen, bool *isdst);
bool tz_local_to_utc(const char *zone, int64 local, int64 *utc);

/* support.c: a zone resolved from a name, either fixed-offset or IANA */
typedef struct PgZone PgZone;
struct PgZone { bool isfixed; int fixed; char name[80]; };
bool pgzone_resolve(const char *name, PgZone *out);
bool tz_abbrev_offset(const char *ab, int *off);
int  tz_offset_utc(const PgZone *z, int64 utc_usec);
int  tz_offset_local(const PgZone *z, int64 local_usec);

void sysviews_init(void);
QueryResult *sysview_scan(Session *s, const char *schema, const char *name, bool *found);
char *lower_ident(const char *s);
char *deparse_expr_src(Session *s, const char *src, const char *rel, bool qualify);
char *deparse_view(Session *s, Table *t, bool pretty);
Expr *parse_expression_text(Session *s, const char *src);
uint64 hash_datum(Session *s, Datum d);
void  store_save(Database *db);
void  store_load(const char *datadir);
void  store_save_all(void);

#define PGVC_VERSION_STR  PGVC_VERSION_TEXT
extern const char *PGVC_VERSION_TEXT;
extern const char *PGVC_VERSION_NUM;
extern const int   PGVC_MAJOR_NUM;

#endif
