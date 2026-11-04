/* pdfint.h -- definitions shared between the library's own modules.
 * Not installed, not visible through pdf.h. */
#ifndef PDFINT_H
#define PDFINT_H

#include "pdf.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ arena */
typedef struct pdf_arena_block {
    struct pdf_arena_block *next;
    size_t used, cap;
    unsigned char *mem;
} pdf_arena_block;

/* --------------------------------------------------------------- xref map */
#define PDF_XREF_FREE   0
#define PDF_XREF_NORMAL 1
#define PDF_XREF_INSTM  2

typedef struct {
    long long off;      /* type 1: byte offset of "N G obj" */
    int   type;
    int   gen;
    int   stmnum;       /* type 2: object-stream object number */
    int   stmidx;       /* type 2: index inside that stream */
    pdf_obj *cached;    /* parsed object once loaded, else NULL */
    unsigned char loading;  /* recursion guard for /Length-style cycles */
} pdf_xrefent;

struct pdf_crypt;       /* defined in crypt.c */

struct pdf_doc {
    unsigned char *data;
    size_t         len;
    char          *path;

    pdf_arena_block *arena;

    pdf_xrefent *xref;
    int          xrefcap;   /* allocated entries */
    int          maxobj;    /* highest object number seen */

    pdf_obj *trailer;
    pdf_obj *catalog;

    int hdrver;             /* from %PDF-x.y, as x*10+y */
    int ver;                /* hdrver, raised by catalog /Version */
    int damaged;            /* xref was reconstructed */
    int reconstructed;
    /* 7.5.8.2.  A cross-reference stream states how much table data it has
     * twice over -- once as /W times the counts in /Index, once as the stream
     * itself -- and a file that disagrees with itself there has no usable
     * table.  Both numbers are kept, because a reader that refuses names both. */
    int       xrefstm_badsize;
    long long xrefstm_off;      /* offset of the section that disagreed */
    long long xrefstm_exp;      /* bytes /W and /Index promised          */
    long long xrefstm_act;      /* bytes the stream actually decoded to  */
    int       xrefstm_unusable; /* ... and it defined no entry at all     */

    pdf_obj **pages;        /* flattened page tree */
    int       npages;
    int       pages_built;

    struct pdf_crypt *crypt;
    int encrypted;
    int unlocked;

    char err[256];
};

/* Internal helpers implemented in the module named in the comment. */
void  pdf_set_error(pdf_doc *doc, const char *fmt, ...);          /* obj.c   */
int   pdf_xref_load(pdf_doc *doc);                                /* xref.c  */
int   pdf_xref_reconstruct(pdf_doc *doc);                         /* xref.c  */
int   pdf_xref_ensure(pdf_doc *doc, int num);                     /* xref.c  */
pdf_obj *pdf_load_object(pdf_doc *doc, int num);                  /* xref.c  */
int   pdf_apply_filters(pdf_doc *doc, pdf_obj *stmdict,
                        const unsigned char *src, size_t srclen,
                        pdf_buf *out);                            /* filter.c */
int   pdf_build_pages(pdf_doc *doc);                              /* doc.c   */
/* 7.6.1.  Decrypt every string reachable from one freshly parsed INDIRECT
 * object, in place.  Called with the object's own number and generation, which
 * are the key material for every string inside it (Algorithm 1 step a).  It is
 * deliberately not called for objects that came out of an object stream: that
 * stream was decrypted once, as a stream, and its contents are not encrypted a
 * second time. */
void  pdf_crypt_decrypt_obj(pdf_doc *doc, pdf_obj *o, int num, int gen);
/* Decrypt one stream's stored bytes.  Returns 1 when `out` holds the plaintext
 * and 0 when this stream is not encrypted at all -- an unencrypted document,
 * an /Identity crypt filter, or one of 7.6.1's exemptions (a cross-reference
 * stream, or /Metadata under /EncryptMetadata false) -- in which case the
 * caller uses the bytes it already had. */
int   pdf_crypt_stream(pdf_doc *doc, pdf_obj *stm, const unsigned char *src,
                       size_t srclen, pdf_buf *out);
/* 12.7.3.3, implemented in text.c and shared with the writer so that what a
 * page READS and what --generate-appearances WRITES cannot drift apart. */
int   pdf_field_wants_appearance(pdf_doc *doc, pdf_obj *annot, int has_ap);
int   pdf_field_appearance(pdf_doc *doc, pdf_obj *annot, pdf_buf *out,
                           double *w, double *h);

/* Standard-14 font metrics, defined in font_std14.c. Returns NULL when the
 * base font is not one of the 14. */
const short *pdf_std14_widths(const char *basefont, const char **encname);
int          pdf_std14_is(const char *basefont);

#endif /* PDFINT_H */
