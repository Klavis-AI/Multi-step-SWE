/* pdf.h -- object model, document access and module contracts.
 *
 * Memory model: every pdf_obj belongs to the pdf_doc that produced it and is
 * allocated from that document's arena. Objects are never freed individually,
 * so reference cycles (/Parent, /P, /Prev) are harmless and no traversal needs
 * a visited set for memory safety. pdf_close() releases the arena.
 *
 * Error model: functions returning int return 0 on success and a negative
 * PDF_E_* on failure. Functions returning pdf_obj* return the shared null
 * object (never NULL) when a value is absent, so callers can chain without
 * checking at every step; use pdf_is_null() to test.
 */
#ifndef PDF_H
#define PDF_H

#include <stddef.h>
#include <stdio.h>

#define PDF_E_IO        -1
#define PDF_E_SYNTAX    -2
#define PDF_E_NOMEM     -3
#define PDF_E_RANGE     -4
#define PDF_E_UNSUP     -5
#define PDF_E_ENCRYPT   -6   /* needs a password we were not given */
#define PDF_E_DAMAGED   -7

/* ---------------------------------------------------------------- buffers */

typedef struct {
    unsigned char *p;
    size_t n, cap;
} pdf_buf;

void  pdf_buf_init(pdf_buf *b);
int   pdf_buf_put(pdf_buf *b, const void *data, size_t n);
int   pdf_buf_putc(pdf_buf *b, int c);
int   pdf_buf_printf(pdf_buf *b, const char *fmt, ...);
void  pdf_buf_free(pdf_buf *b);

/* ----------------------------------------------------------- object model */

typedef enum {
    PDF_NULL = 0, PDF_BOOL, PDF_INT, PDF_REAL, PDF_STRING,
    PDF_NAME, PDF_ARRAY, PDF_DICT, PDF_STREAM, PDF_REF
} pdf_kind;

typedef struct pdf_obj pdf_obj;
typedef struct pdf_doc pdf_doc;

typedef struct { char *key; pdf_obj *val; } pdf_dictent;

struct pdf_obj {
    pdf_kind kind;
    union {
        int          b;
        long long    i;
        /* PDF_REAL: the value, plus the literal token exactly as it was
         * written in the file.  qpdf echoes a real verbatim rather than
         * reformatting it, so ".25" must not come back as "0.25" and "2.50"
         * must keep its trailing zero; lit is NULL for a real this library
         * built itself rather than parsed. */
        struct { double v; const char *lit; } real;
        struct { char *p; size_t n; } str;    /* PDF_STRING: raw bytes (decrypted) */
        struct { char *p; } name;             /* PDF_NAME: #xx decoded, NUL-terminated */
        struct { pdf_obj **v; size_t n, cap; } arr;
        struct { pdf_dictent *e; size_t n, cap; } dict;
        struct {
            pdf_obj *dict;                    /* PDF_DICT holding the stream dictionary */
            long long off;                    /* file offset of first data byte, or -1 */
            long long len;                    /* raw length in bytes, or -1 if unknown */
            unsigned char *mem;               /* non-NULL when data is already in memory */
            size_t memlen;
            int num, gen;                     /* owning indirect object, for decryption */
        } stm;
        struct { int num, gen; } ref;
    } u;
};

pdf_obj *pdf_null_obj(void);
int  pdf_is_null(const pdf_obj *o);
int  pdf_is_int(const pdf_obj *o);
int  pdf_is_num(const pdf_obj *o);          /* int or real */
int  pdf_is_name(const pdf_obj *o, const char *name);  /* name==NULL: any name */
int  pdf_is_dict(const pdf_obj *o);         /* true for PDF_DICT and PDF_STREAM */
int  pdf_is_array(const pdf_obj *o);
int  pdf_is_string(const pdf_obj *o);
int  pdf_is_stream(const pdf_obj *o);

long long   pdf_to_int(const pdf_obj *o);
double      pdf_to_real(const pdf_obj *o);
const char *pdf_to_name(const pdf_obj *o);          /* "" when not a name */
const char *pdf_to_string(const pdf_obj *o, size_t *len);

/* Constructors. All allocate from doc's arena; doc may be NULL for the
 * scratch arena used by the writer before a document exists. */
pdf_obj *pdf_new_bool(pdf_doc *doc, int v);
pdf_obj *pdf_new_int(pdf_doc *doc, long long v);
pdf_obj *pdf_new_real(pdf_doc *doc, double v);
/* As pdf_new_real(), but remembering the literal token `lit` (n bytes) the
 * value was written as, for a caller that must reproduce the file's spelling. */
pdf_obj *pdf_new_real_lit(pdf_doc *doc, double v, const char *lit, size_t n);
pdf_obj *pdf_new_string(pdf_doc *doc, const char *p, size_t n);
pdf_obj *pdf_new_name(pdf_doc *doc, const char *name);
pdf_obj *pdf_new_array(pdf_doc *doc);
pdf_obj *pdf_new_dict(pdf_doc *doc);
pdf_obj *pdf_new_ref(pdf_doc *doc, int num, int gen);

int      pdf_array_push(pdf_doc *doc, pdf_obj *arr, pdf_obj *item);
size_t   pdf_array_len(const pdf_obj *arr);
pdf_obj *pdf_array_get_raw(const pdf_obj *arr, size_t i);     /* no resolve */
int      pdf_dict_put(pdf_doc *doc, pdf_obj *dict, const char *key, pdf_obj *val);
void     pdf_dict_del(pdf_obj *dict, const char *key);
size_t   pdf_dict_len(const pdf_obj *dict);
const char *pdf_dict_key(const pdf_obj *dict, size_t i);
pdf_obj *pdf_dict_val_raw(const pdf_obj *dict, size_t i);
pdf_obj *pdf_dict_get_raw(const pdf_obj *dict, const char *key);  /* no resolve */

/* ------------------------------------------------------------- documents */

pdf_doc *pdf_open(const char *path, char **errmsg);
int      pdf_authenticate(pdf_doc *doc, const char *password);
int      pdf_is_encrypted(pdf_doc *doc);
int      pdf_is_unlocked(pdf_doc *doc);
void     pdf_close(pdf_doc *doc);

const char *pdf_error(pdf_doc *doc);        /* last error text, never NULL */
int         pdf_damaged(pdf_doc *doc);      /* xref had to be reconstructed */

/* Version as an integer x10 (13 == "1.3", 20 == "2.0"), taking the catalog
 * /Version override into account per ISO 32000-1 7.5.2. */
int       pdf_version(pdf_doc *doc);
int       pdf_header_version(pdf_doc *doc);
long long pdf_file_size(pdf_doc *doc);

pdf_obj *pdf_trailer(pdf_doc *doc);
pdf_obj *pdf_catalog(pdf_doc *doc);
pdf_obj *pdf_resolve(pdf_doc *doc, pdf_obj *o);
pdf_obj *pdf_get_object(pdf_doc *doc, int num, int gen);
int      pdf_max_objnum(pdf_doc *doc);

/* Cross-reference introspection.  One entry as the table finally resolved it,
 * which after an incremental update is the revision that won: *type is 1 for an
 * uncompressed object and *off its byte offset.  Returns 0 for a free or absent
 * entry, so a caller may iterate 1..pdf_max_objnum() and print what exists. */
int      pdf_xref_entry(pdf_doc *doc, int num, int *type, long long *off, int *gen);

/* Re-walk startxref and the /Prev chain looking only for sections that are
 * really there.  Returns 1 when a hop lands on nothing, with *badoff set to the
 * offset that was promised -- the diagnosis a reader needs before falling back
 * on reconstruction, which pdf_open() otherwise does silently.  Returns 2 when
 * startxref names offset 0, which is a different diagnosis: offset 0 is the
 * header, so the file names no cross-reference section at all. */
#define PDF_XREF_CHAIN_LOST     1   /* a hop landed on nothing   */
#define PDF_XREF_CHAIN_NOSTART  2   /* startxref names offset 0  */
int      pdf_xref_chain_check(pdf_doc *doc, long long *badoff);

/* The newest section's own /Prev, which the loader drops when it merges the
 * sections into one trailer (it describes a section, not the document) and
 * which the file's trailer nonetheless carries.  Returns 0 when there is none
 * to report, including when the newest section is not really there. */
int      pdf_xref_trailer_prev(pdf_doc *doc, long long *prev);
/* Did the NEWEST section that mentions this object mark it free?  A free
 * entry is never recorded, so an older section's definition is otherwise left
 * standing -- right for recovery, wrong for an update whose purpose was to
 * delete the object. */
int      pdf_xref_freed_by_newer(pdf_doc *doc, int num);
void     pdf_xref_force_reconstruct(pdf_doc *doc);

/* The file image itself, for a caller that must look at a byte offset the
 * object model does not reach: a linearization hint stream is named by offset,
 * and whether an object header is exactly there is the question being asked. */
const unsigned char *pdf_file_bytes(pdf_doc *doc, size_t *len);

/* ------------------------------ cross-reference streams and object streams */

/* 7.5.8.2.  "The cross-reference stream's dictionary shall serve as the
 * trailer."  Returns that dictionary when the newest section -- the one
 * startxref names -- is a stream, and a null object when it is a classic table
 * or is not there at all.  It is the file's own dictionary, so unlike
 * pdf_trailer() it still carries /Type, /W, /Index, /Length and /Prev. */
pdf_obj *pdf_xref_stream_dict(pdf_doc *doc);

/* 7.5.8.2 type 2: an object stored inside an object stream has no file offset,
 * so pdf_xref_entry()'s *off says nothing about it.  Returns 1 and fills in the
 * containing stream's object number and the index within it; 0 for every entry
 * that is not a compressed one. */
int pdf_xref_compressed(pdf_doc *doc, int num, int *stmnum, int *stmidx);

/* As pdf_xref_chain_check(), for a chain whose hops are cross-reference
 * STREAMS.  That walk reads each section's /Prev out of a classic trailer only,
 * so on a 1.5 file it never takes a second hop and calls a broken chain intact.
 * Ask this only after that one has found nothing. */
int pdf_xref_stream_chain_check(pdf_doc *doc, long long *badoff);

/* Did a cross-reference stream promise a different amount of data than it
 * carried (7.5.8.2)?  Returns 1 with the section offset and the two byte
 * counts; the rows are then not where /W says they are, so the table is not
 * readable at all. */
int pdf_xref_stream_size_error(pdf_doc *doc, long long *off,
                               long long *expected, long long *actual);

/* ... and did that section then define no entry whatsoever?  Only then is the
 * disagreement fatal: a stream with data to spare still has its rows where /W
 * says they are, while one that yielded nothing had none of them there. */
int pdf_xref_stream_unusable(pdf_doc *doc);

/* 7.5.7.  What is wrong with the object stream numbered `stmnum`, judged from
 * where the cross-reference table says it is -- so a reader can say why a
 * compressed object could not be produced instead of silently recovering from
 * it.  *off and *gen describe the object as the table names it. */
#define PDF_OBJSTM_NULL       1     /* nothing resolvable is at that offset  */
#define PDF_OBJSTM_WRONGTYPE  2     /* it is not a /Type /ObjStm             */
#define PDF_OBJSTM_BADKEYS    4     /* /N or /First is missing or not an int */
int pdf_objstm_diagnose(pdf_doc *doc, int stmnum, long long *off, int *gen);

/* Resolving accessors: these follow PDF_REF. */
pdf_obj *pdf_dict_get(pdf_doc *doc, pdf_obj *dict, const char *key);
pdf_obj *pdf_dict_geta(pdf_doc *doc, pdf_obj *dict, const char *k1, const char *k2);
pdf_obj *pdf_array_get(pdf_doc *doc, pdf_obj *arr, size_t i);

/* decode!=0 applies the /Filter chain (and decryption); decode==0 returns raw
 * bytes after decryption only. */
int pdf_stream_data(pdf_doc *doc, pdf_obj *stm, pdf_buf *out, int decode);

/* ------------------------------------------------------------- page tree */

int       pdf_page_count(pdf_doc *doc);
pdf_obj  *pdf_page_dict(pdf_doc *doc, int index);            /* 0-based */
/* The object number a flattened page came from, or -1.  Needed to report a page
 * as "N G R" rather than by its contents. */
int       pdf_page_objnum(pdf_doc *doc, int index, int *gen);
pdf_obj  *pdf_page_inherited(pdf_doc *doc, pdf_obj *page, const char *key);

typedef struct { double x0, y0, x1, y1; } pdf_rect;
int  pdf_page_box(pdf_doc *doc, pdf_obj *page, const char *boxname, pdf_rect *out);
int  pdf_page_rotate(pdf_doc *doc, pdf_obj *page);           /* normalised 0/90/180/270 */
int  pdf_page_content(pdf_doc *doc, pdf_obj *page, pdf_buf *out);

/* ---------------------------------------------------------------- lexer */

typedef enum {
    PDF_TOK_EOF = 0, PDF_TOK_ERROR, PDF_TOK_INT, PDF_TOK_REAL, PDF_TOK_STRING,
    PDF_TOK_NAME, PDF_TOK_ARRAY_OPEN, PDF_TOK_ARRAY_CLOSE, PDF_TOK_DICT_OPEN,
    PDF_TOK_DICT_CLOSE, PDF_TOK_KEYWORD, PDF_TOK_BRACE_OPEN, PDF_TOK_BRACE_CLOSE
} pdf_toktype;

typedef struct {
    const unsigned char *buf;
    size_t len, pos;
} pdf_lexer;

typedef struct {
    pdf_toktype type;
    long long   i;
    double      r;
    char       *s;      /* NAME: decoded name; STRING: bytes; KEYWORD: text;
                           REAL: the literal token, exactly as written */
    size_t      slen;
} pdf_token;

void pdf_lex_init(pdf_lexer *lx, const unsigned char *buf, size_t len, size_t pos);
int  pdf_lex_next(pdf_doc *doc, pdf_lexer *lx, pdf_token *tok);
pdf_obj *pdf_parse_obj(pdf_doc *doc, pdf_lexer *lx);

/* -------------------------------------------------------------- filters */

int pdf_filter_flate(pdf_doc *doc, const unsigned char *src, size_t srclen,
                     pdf_obj *parms, pdf_buf *out);
int pdf_filter_lzw(pdf_doc *doc, const unsigned char *src, size_t srclen,
                   pdf_obj *parms, pdf_buf *out);
int pdf_filter_a85(const unsigned char *src, size_t srclen, pdf_buf *out);
int pdf_filter_ahx(const unsigned char *src, size_t srclen, pdf_buf *out);
int pdf_filter_rl(const unsigned char *src, size_t srclen, pdf_buf *out);
int pdf_filter_ccitt(pdf_doc *doc, const unsigned char *src, size_t srclen,
                     pdf_obj *parms, pdf_buf *out);
int pdf_predictor(pdf_doc *doc, pdf_buf *buf, pdf_obj *parms);

/* --------------------------------------------------------------- crypto */

int  pdf_crypt_init(pdf_doc *doc, pdf_obj *encrypt, pdf_obj *id0);
int  pdf_crypt_authenticate(pdf_doc *doc, const char *pw);
int  pdf_crypt_data(pdf_doc *doc, int num, int gen, int is_string,
                    const unsigned char *src, size_t srclen, pdf_buf *out);
int  pdf_crypt_revision(pdf_doc *doc);
int  pdf_crypt_version(pdf_doc *doc);
long long pdf_crypt_perms(pdf_doc *doc);
const char *pdf_crypt_method(pdf_doc *doc);

/* ----------------------------------------------------------- text / fonts */

typedef struct pdf_font pdf_font;

typedef struct {
    unsigned int ucs[8];    /* Unicode scalar values this glyph maps to */
    int          nucs;
    double       x, y;      /* origin, page space, y up */
    double       w, h;      /* advance width, font size */
    int          space_after;
} pdf_glyph;

typedef struct {
    pdf_glyph *g;
    size_t     n, cap;
} pdf_glyphs;

int pdf_extract_glyphs(pdf_doc *doc, pdf_obj *page, pdf_glyphs *out);
int pdf_glyphs_to_text(pdf_glyphs *g, int raw, pdf_buf *out);
void pdf_glyphs_free(pdf_glyphs *g);

typedef struct {
    char name[192];
    char type[40];      /* "Type 1", "TrueType", "CID Type 0C", ... */
    char encoding[64];
    int  emb, sub, uni;
    int  objnum, objgen;
} pdf_fontinfo;

int pdf_list_fonts(pdf_doc *doc, int first, int last,
                   pdf_fontinfo **out, size_t *nout);

/* --------------------------------------------------------------- utility */

int  pdf_utf8_put(pdf_buf *b, unsigned int cp);
int  pdf_text_string_to_utf8(const char *s, size_t n, pdf_buf *out);
int  pdf_fmt_real(pdf_buf *b, double v);

void *pdf_alloc(pdf_doc *doc, size_t n);
void *pdf_calloc(pdf_doc *doc, size_t count, size_t n);
char *pdf_strdup_n(pdf_doc *doc, const char *s, size_t n);

#endif /* PDF_H */
