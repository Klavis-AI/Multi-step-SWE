/* zend_API.h — runtime services shared by the evaluator and the extensions. */
#ifndef ZEND_API_H
#define ZEND_API_H
#include "zend.h"
#include "zend_ast.h"

/* --------------------------------------------------------------- globals */
typedef struct _zend_execute_data zend_execute_data;

typedef struct {
    HashTable *function_table;     /* lcname => zend_function*   */
    HashTable *class_table;        /* lcname => zend_class_entry* */
    HashTable *constants;          /* name => zval                */
    HashTable *symbol_table;       /* $GLOBALS                    */
    HashTable *included_files;
    HashTable *ini;                /* name => zend_string*        */
    zend_execute_data *current;
    zend_object *exception;
    zend_string *filename;         /* current script              */
    uint32_t     lineno;
    int          silence;          /* @ operator depth            */
    int          exit_status;
    int          exiting;
    long         php_version;      /* 50640, 80400 ...            */
    const char  *php_version_str;
    uint32_t     next_obj_handle;
    int          error_reporting;
    int          in_shutdown;
    HashTable   *autoloaders;
    int          strict_types;     /* per-file declare(strict_types=1) */
    HashTable   *interned;
    int          suppress_deprecations;
} zend_globals;

extern zend_globals EG_;
#define EG(x) (EG_.x)

struct _zend_execute_data {
    zend_execute_data *prev;
    HashTable   *symbols;          /* local variables, name => zval (may be IS_REFERENCE) */
    zend_function *func;
    zend_class_entry *called_scope;
    zend_object *This;
    zend_string *filename;
    uint32_t     lineno;           /* call site line */
    zval        *args;             /* evaluated arguments, for func_get_args() */
    uint32_t     num_args;
    zend_string **arg_names;       /* named arguments, NULL entries for positional */
    struct _zend_generator *generator;
    int          is_arrow_fn;
};

/* ----------------------------------------------------------- error output */
#define E_ERROR             1
#define E_WARNING           2
#define E_PARSE             4
#define E_NOTICE            8
#define E_CORE_ERROR       16
#define E_CORE_WARNING     32
#define E_COMPILE_ERROR    64
#define E_COMPILE_WARNING 128
#define E_USER_ERROR      256
#define E_USER_WARNING    512
#define E_USER_NOTICE    1024
#define E_STRICT         2048
#define E_RECOVERABLE_ERROR 4096
#define E_DEPRECATED     8192
#define E_USER_DEPRECATED 16384
#define E_ALL            32767

void zend_error(int type, const char *fmt, ...);
void zend_error_at(int type, zend_string *file, uint32_t line, const char *fmt, ...);
void zend_fatal(const char *fmt, ...);          /* E_ERROR: prints and exits */
void php_printf(const char *fmt, ...);
void php_write(const char *s, size_t len);
void php_write_str(zend_string *s);

/* ------------------------------------------------------------ conversions */
zend_string *zval_get_string(const zval *v);        /* may run __toString  */
zend_long    zval_get_long(const zval *v);
double       zval_get_double(const zval *v);
int          zval_is_true(const zval *v);
const char  *zend_get_type_name(const zval *v);     /* gettype() name      */
const char  *zend_zval_type_name(const zval *v);    /* 8.0 type error name */
void         convert_to_string(zval *v);
void         convert_to_long(zval *v);
void         convert_to_double(zval *v);
void         convert_to_boolean(zval *v);
void         convert_to_array(zval *v);
int          is_numeric_string(const char *s, size_t len, zend_long *lval, double *dval, int allow_errors);

/* comparison and arithmetic; return 0 on success, 1 when an exception was thrown */
int  zend_compare(const zval *a, const zval *b, int *result);
int  zend_is_equal(const zval *a, const zval *b);
int  zend_is_identical(const zval *a, const zval *b);
int  zend_binary_op(zend_binop op, const zval *a, const zval *b, zval *result);

/* ------------------------------------------------------------ zval basics */
#define ZVAL_UNDEF(z)  do { (z)->u1 = IS_UNDEF; } while (0)
#define ZVAL_NULL(z)   do { (z)->u1 = IS_NULL; } while (0)
#define ZVAL_BOOL(z,b) do { (z)->u1 = (b) ? IS_TRUE : IS_FALSE; } while (0)
#define ZVAL_LONG(z,l) do { (z)->u1 = IS_LONG; (z)->value.lval = (l); } while (0)
#define ZVAL_DOUBLE(z,d) do { (z)->u1 = IS_DOUBLE; (z)->value.dval = (d); } while (0)
#define ZVAL_STR(z,s)  do { (z)->u1 = IS_STRING; (z)->value.str = (s); } while (0)
#define ZVAL_ARR(z,a)  do { (z)->u1 = IS_ARRAY; (z)->value.arr = (a); } while (0)
#define ZVAL_OBJ(z,o)  do { (z)->u1 = IS_OBJECT; (z)->value.obj = (o); } while (0)

zval zval_null(void);
zval zval_bool(int b);
zval zval_long(zend_long l);
zval zval_double(double d);
zval zval_string(const char *s);
zval zval_stringl(const char *s, size_t len);
zval zval_zstr(zend_string *s);
zval zval_array(HashTable *ht);
zval zval_object(zend_object *o);
zval zval_copy(zval v);                 /* value semantics: arrays share + rc++ */
zval *zval_deref(zval *v);              /* follow IS_REFERENCE                 */
void  zval_separate_array(zval *v);     /* copy-on-write split                 */
zval  zval_clone_deep(zval v);

/* -------------------------------------------------------------- functions */
zend_function *zend_lookup_function(const zend_string *lcname);
zend_class_entry *zend_lookup_class(const zend_string *name, int autoload);
zend_function *zend_lookup_method(zend_class_entry *ce, const zend_string *lcname);
int zend_instanceof(zend_class_entry *ce, zend_class_entry *target);

/* call a function/method; returns 0 on success, 1 if an exception is pending */
int zend_call_function_ex(zend_function *fn, zend_object *thisptr,
                          zend_class_entry *called_scope,
                          uint32_t argc, zval *argv, zend_string **argnames,
                          zval *retval, zval **arg_slots);
int zend_call_user_callable(zval *callable, uint32_t argc, zval *argv, zval *retval);
int zend_is_callable_zval(zval *callable, zend_function **fn, zend_object **obj,
                          zend_class_entry **scope, zend_string **err);

/* ------------------------------------------------------------- exceptions */
zend_object *zend_throw_error(const char *class_name, const char *fmt, ...);
void zend_throw_exception_object(zend_object *obj);
#define EG_HAS_EXCEPTION() (EG(exception) != NULL)

/* ------------------------------------------------------------ array utils */
zval *array_get(HashTable *ht, const zval *key);
zval *array_set(HashTable *ht, const zval *key, zval value);
int   array_key_normalize(const zval *key, zend_long *idx, zend_string **str);

/* --------------------------------------------------------------- printing */
void php_var_dump(zval *v, int level);
void php_print_r(zval *v, int level, int in_property);
void php_var_export(zval *v, int level);
zend_string *php_json_encode(zval *v, zend_long options, int *err);
int  php_json_decode(const char *str, size_t len, int assoc, zend_long depth, zend_long options, zval *ret);
zend_string *php_sprintf(const char *fmt, size_t fmtlen, uint32_t argc, zval *argv, int *err);
zend_string *php_double_to_string(double d, int precision, int mode);
zend_string *php_long_to_string(zend_long l);
zend_string *smart_double_repr(double d);        /* serialize_precision = -1 */

/* --------------------------------------------------------- registration */
typedef struct {
    const char *name;
    void (*handler)(zval *ret, uint32_t argc, zval *argv);
    uint32_t min_args;
    uint32_t max_args;              /* -1u for variadic */
    const char *ref_mask;           /* e.g. "01" -> argument 1 taken by reference */
    long since;                     /* first version, e.g. 70000; 0 = always */
    long until;                     /* removed in version, 0 = never */
} php_function_entry;

extern const php_function_entry php_builtin_functions[];
void zend_register_functions(void);
void zend_register_constants(void);
void zend_register_classes(void);

/* argument helpers used by builtins */
#define ARG(i)      (&argv[i])
#define ARG_DEREF(i) zval_deref(&argv[i])
int zend_parse_arg_long(zval *v, zend_long *dest, int arg_num, const char *fname);
int zend_parse_arg_string(zval *v, zend_string **dest, int arg_num, const char *fname);
int zend_parse_arg_double(zval *v, double *dest, int arg_num, const char *fname);
void zend_wrong_param_count(const char *fname, uint32_t given, uint32_t min, uint32_t max);

/* ---------------------------------------------------------------- objects */
zend_object *zend_object_new(zend_class_entry *ce);
int  zend_object_construct(zend_object *obj, uint32_t argc, zval *argv, zend_string **names);
zval *zend_read_property(zend_object *obj, zend_string *name, int silent, zval *tmp);
int  zend_write_property(zend_object *obj, zend_string *name, zval value);
zend_string *zend_mangle_property_name(zend_class_entry *ce, zend_string *name, uint32_t flags);
zend_string *zend_unmangle_property_name(zend_string *mangled, zend_class_entry **ce_out);
int  zend_object_to_string(zend_object *obj, zend_string **out);

/* ------------------------------------------------------------- evaluation */
typedef enum { EX_NORMAL = 0, EX_RETURN, EX_BREAK, EX_CONTINUE, EX_GOTO, EX_EXCEPTION, EX_EXIT } ex_status;
ex_status zend_execute_ast(zend_ast *ast, zval *retval);
int  zend_eval_expr(zend_ast *ast, zval *result);      /* 1 = exception */
zval *zend_get_lval(zend_ast *ast, int create);        /* NULL on error */
void zend_execute_script(zend_ast *ast);

/* main entry points */
zend_ast *php_parse_file(const char *filename, zend_string **err);
zend_ast *php_parse_string(const char *code, size_t len, zend_string *filename, zend_string **err);

#endif
