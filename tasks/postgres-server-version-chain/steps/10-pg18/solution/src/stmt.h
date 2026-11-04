#ifndef PGVC_STMT_H
#define PGVC_STMT_H
#include "engine.h"

typedef struct ColDef {
    int   typepos;
    char *name;
    char *typename;
    Oid   type; int32 typmod;
    bool  notnull, isnull_spec, unique, primary;
    char *defexpr;
    char  generated;          /* 0 's' 'v' */
    char *genexpr;
    struct Expr *genexpr_e;
    int   virtual_pos;        /* char position of a VIRTUAL keyword */
    char  identity;           /* 0 'a' 'd' */
    int   identity_pos;       /* char position of the GENERATED keyword */
    int   default_pos;        /* char position of the DEFAULT keyword */
    char *compression;        /* COMPRESSION pglz|lz4|default */
    bool  nulls_nd;           /* UNIQUE NULLS NOT DISTINCT */
    int   cons_pos;           /* char position of PRIMARY/UNIQUE/REFERENCES */
    int64 seq_start, seq_inc, seq_min, seq_max, seq_cache;
    bool  seq_cycle, has_seq_start, has_seq_inc, has_seq_min, has_seq_max;
    char *checksrc;
    char *collation;
    Oid   ref_table_dummy;
    char *fk_table; char **fk_cols; int nfk_cols; char fk_upd, fk_del;
    int   genexpr_pos, generated_pos;
} ColDef;

typedef struct TblCons {
    char  contype;            /* p u c f x */
    int   pos;                /* char position the constraint clause starts at */
    char *name;
    char **cols; int ncols;
    bool  without_overlaps;
    char *checksrc;
    char *fk_table; char **fk_cols; int nfk_cols; char fk_upd, fk_del; bool fk_period;
    char **incl; int nincl;
    bool  not_valid, noinherit, not_enforced;
    int  *colpos;             /* char position of each key column name */
    bool  nulls_nd;           /* UNIQUE NULLS NOT DISTINCT */
} TblCons;

typedef struct CreateTableStmt {
    char *schema, *name;
    int   name_pos;           /* char position of the relation name */
    bool  if_not_exists, temporary, unlogged;
    ColDef **cols; int ncols;
    TblCons **cons; int ncons;
    /* partitioning */
    char  partstrat;          /* 0 r l h */
    char **partkey; int npartkey;
    char **partkeyexpr;       /* parallel to partkey; set for expression keys */
    int   bound_pos;          /* char position of FROM/IN/WITH in the bound spec */
    bool  has_modulus;        /* MODULUS was written, whatever its value */
    /* PARTITION OF */
    char *parent_schema, *parent_name;
    char *bound_text;
    bool  bound_default;
    Expr ***bound_from; int nbound_from;
    Expr ***bound_to;   int nbound_to;
    Expr  **bound_in;   int nbound_in;
    int   bound_modulus, bound_remainder;
    /* CREATE TABLE AS / LIKE */
    Node *as_query;
    char *like_table;
    bool  with_oids; int oids_pos;
    char *using_am; int using_pos;
    char **inherits; int ninherits;
    char **reloptions; char **reloptvals; int nreloptions;
} CreateTableStmt;

typedef struct CreateIndexStmt {
    char *name, *schema, *table;
    bool  unique, concurrent, if_not_exists, only;
    char **cols; int ncols;
    Expr **colexprs;
    char **incl; int nincl;
    char *method;
    char *predicate;
    int  *dir; int *nulls;
    char **optname, **optval; int nopts; int *optpos;
    char **exprs; int nexprs;   /* CREATE STATISTICS expression keys */
    bool  nulls_nd;             /* UNIQUE ... NULLS NOT DISTINCT */
    int   stats_alter;          /* 0 create, 1 SET STATISTICS, 2 RENAME TO */
    int   stattarget;
    char *newname;
} CreateIndexStmt;

typedef struct CreateViewStmt {
    char *schema, *name; bool replace, recursive, materialized;
    char **cols; int ncols;
    Node *query; char *querysrc;
    bool security_invoker, security_barrier;
    char **optname, **optval; int nopts;
    int  check_option;        /* 0 none 1 local 2 cascaded */
} CreateViewStmt;

typedef struct DropStmt {
    char *objtype;            /* table view index sequence schema role publication
                                 function procedure type database subscription */
    char ***names; int nnames;  /* each is {schema,name} */
    bool if_exists, cascade;
    char **argsig; int nargsig; /* routine argument type list, one per name */
    int   *nargcount;
    char *onrel;              /* DROP TRIGGER ... ON <rel> */
} DropStmt;

typedef struct AlterTableCmd {
    int   kind;   /* 1 add col, 2 drop col, 3 alter col type, 4 set default, 5 drop default,
                     6 set not null, 7 drop not null, 8 add constraint, 9 drop constraint,
                     10 attach partition, 11 detach partition, 12 rename, 13 rename col,
                     14 owner, 15 set schema, 16 add identity, 17 drop identity, 18 set/reset opts,
                     19 enable/disable, 20 replica identity, 21 alter col set generated */
    ColDef *col;
    char   *name, *name2;
    TblCons *cons;
    char   *bound_text;
    CreateTableStmt *bound;
    bool    if_exists, cascade, missing_ok, concurrently, finalize;
    char   *newtype; Oid type; int32 typmod; char *using_expr;
    int64   seq_arg; int seq_what;   /* 1 RESTART, 2 SET INCREMENT, 3 SET START */
    /* ALTER SEQUENCE option list (kind 23) */
    int64   sq_inc, sq_min, sq_max, sq_start, sq_restart, sq_cache;
    bool    sq_has_inc, sq_has_min, sq_has_max, sq_has_start, sq_has_restart;
    bool    sq_has_cache, sq_cycle, sq_has_cycle;
    char **optname, **optval; int nopts; int *optpos;   /* SET/RESET (...) */
} AlterTableCmd;

typedef struct AlterTableStmt {
    char *schema, *name; bool if_exists, only;
    char *objtype;            /* table, index, sequence, view, materialized view */
    AlterTableCmd **cmds; int ncmds;
} AlterTableStmt;

typedef struct InsertStmt {
    char *schema, *name; int pos;
    char **cols; int ncols;
    Node *query;                 /* SELECT or VALUES */
    bool  default_values;
    int   overriding;            /* 0 none 1 system 2 user */
    Expr **returning; int nret;
    int   onconflict;            /* 0 none 1 do nothing 2 do update */
    char **conf_cols; int nconf_cols; char *conf_constraint;
    char **set_cols; int nset_cols; Expr **set_vals;
    Expr  *conf_where, *set_where;
    CteDef **ctes; int nctes;
} InsertStmt;

typedef struct UpdateStmt {
    char *schema, *name; char *alias; bool only;
    char **cols; int ncols; Expr **vals;
    RangeTbl **from; int nfrom;
    Expr *where;
    Expr **returning; int nret;
    CteDef **ctes; int nctes;
} UpdateStmt;

typedef struct DeleteStmt {
    char *schema, *name; char *alias; bool only;
    RangeTbl **using_; int nusing;
    Expr *where;
    Expr **returning; int nret;
    CteDef **ctes; int nctes;
} DeleteStmt;

typedef struct MergeWhen {
    int   kind;      /* 0 matched, 1 not matched (by target), 2 not matched by source */
    Expr *cond;
    int   action;    /* 0 update, 1 delete, 2 insert, 3 do nothing */
    char **cols; int ncols; Expr **vals;
    bool  ins_default_values;
    int   overriding;
    int  *colpos;
} MergeWhen;

typedef struct MergeStmt {
    char *schema, *name, *alias;
    RangeTbl *source;
    Expr *on;
    MergeWhen **whens; int nwhens;
    Expr **returning; int nret;
    CteDef **ctes; int nctes;
    int   relpos;
} MergeStmt;

typedef struct CopyStmt {
    char *schema, *name;
    char **cols; int ncols;
    Node *query;
    bool  from;                 /* COPY ... FROM */
    bool  stdin_, stdout_;
    char *filename;
    char *format;               /* text csv binary */
    char  delim, quote, escape;
    bool  header;
    char *null_str;
    bool  freeze;
    char **force_not_null; int nforce_not_null;
    Expr  *where; int where_pos;          /* COPY ... FROM ... WHERE (PostgreSQL 12) */
    char *on_error; int on_error_pos;      /* PostgreSQL 17 ON_ERROR */
    char *log_verbosity;
    bool  header_match, bad_header;
} CopyStmt;

typedef struct CreateSeqStmt {
    char *schema, *name; bool if_not_exists, temp;
    int64 start, inc, minv, maxv, cache; bool cycle;
    bool has_start, has_min, has_max;
    char *astype;
    char *owned_by;
} CreateSeqStmt;

typedef struct CreateRoleStmt {
    char *name; bool login, superuser, createdb, createrole, replication, inherit;
    char *password; bool has_password; bool is_user;
    char **inroles; int ninroles;
} CreateRoleStmt;

typedef struct CreatePubStmt {
    char *name; bool alltables;
    char ***tables; int ntables;
    char **where; char ***collist; int *ncollist;
    bool insert_, update_, delete_, truncate_, viaroot;
    bool has_publish;
    char *bad_publish;        /* unrecognized value of the "publish" option */
    char *bad_param;          /* unrecognized WITH parameter name */
    int   alter_kind;         /* ALTER PUBLICATION: 0 options, 1 ADD, 2 SET, 3 DROP,
                                 4 OWNER TO, 5 RENAME TO */
    char *newname;
    bool  has_viaroot; char *bad_bool; bool has_publish_opt;
    int  *wherepos;
} CreatePubStmt;

typedef struct CreateFuncStmt {
    char *schema, *name; bool replace; char prokind;
    char **argnames; char **argtypes; char *argmodes; int nargs;
    char **argdefaults;
    char *rettype; bool retset;
    char *body, *lang; char volatility; bool strict_; char *secdef;
    int   body_pos;            /* char position where the body text starts */
} CreateFuncStmt;

typedef struct TransactionStmt {
    int kind;   /* 0 begin, 1 commit, 2 rollback, 3 savepoint, 4 rollback to,
                   5 release, 6 start, 7 set transaction, 8 prepare, 9 commit prepared */
    char *name;
    int  isolation;  /* -1 unset */
    int  readonly;   /* -1 unset */
    int  deferrable;
} TransactionStmt;

typedef struct VarSetStmt {
    char *name; char *value; bool is_local, is_default, is_reset, is_all;
    char **values; int nvalues;
} VarSetStmt;

typedef struct ExplainStmt { Node *query; bool analyze_, verbose, costs, buffers, wal;
                            char *format; int errkind; char *errarg; int errpos; } ExplainStmt;
typedef struct PrepareStmt { char *name; char **argtypes; int nargs; Node *query; char *src; } PrepareStmt;
typedef struct ExecuteStmt { char *name; Expr **args; int nargs; } ExecuteStmt;
typedef struct CallStmt    { char *schema, *name; Expr **args; int nargs; int pos; } CallStmt;
typedef struct TruncateStmt{ char ***names; int nnames; bool cascade, restart; } TruncateStmt;
typedef struct ReindexStmt { char *kind; char *name; bool concurrent; } ReindexStmt;
typedef struct VacuumStmt { char *badopt; int badpos; char *tag;
                           bool full, par_seen; int parallel;
                           int errkind, errpos2; } VacuumStmt;
typedef struct GrantStmt   { bool is_grant; char *privs; char *objtype; char **objs; int nobjs; char **roles; int nroles; } GrantStmt;
typedef struct CreateDbStmt{ char *name; char *template_; char *encoding; char *owner; } CreateDbStmt;
typedef struct CreateDomainStmt {
    char *name; char *typename; Oid type; int32 typmod;
    char *checksrc; bool notnull;
} CreateDomainStmt;

typedef struct CreateSubStmt {
    char *name; char *conninfo; char **pubs; int npubs;
    bool connect;                  /* WITH (connect = ...), default true */
    bool failover, enabled, binary, disableonerr;
    char *origin;                  /* NULL means the option was not written */
    char *slotname;                /* NULL with has_slotname means slot_name = NONE */
    bool has_connect, has_failover, has_enabled, has_binary, has_slotname;
    char *bad_bool;                /* option whose value is not a Boolean */
    char *bad_param;               /* option name the release does not know */
    int   nopts;                   /* options the WITH/SET list carried */
    int   alter_kind;              /* ALTER SUBSCRIPTION: 0 SET (...), 1 other */
} CreateSubStmt;
typedef struct CursorStmt { char *name; Node *query; bool binary, scroll, hold;
                           int dir; long count; bool all;
                           int seek; long seekn; /* 1 FIRST 2 LAST 3 ABSOLUTE 4 RELATIVE */
                         } CursorStmt;
typedef struct ReplStmt    { char *slot; char *plugin; char *lsn; char **opts; int nopts; bool temporary; bool logical; bool failover; } ReplStmt;

typedef struct CreateTrigStmt {
    char *name, *schema, *table;
    int   timing;              /* 0 BEFORE, 1 AFTER, 2 INSTEAD OF */
    int   events;              /* 1 INSERT | 2 UPDATE | 4 DELETE | 8 TRUNCATE */
    bool  forrow;
    char *funcname;
    char *whensrc;
    char *oldtable, *newtable;
    int   nold, nnew;          /* how often each transition clause appeared */
} CreateTrigStmt;

typedef struct DoStmt { char *body; char *lang; } DoStmt;

#endif
