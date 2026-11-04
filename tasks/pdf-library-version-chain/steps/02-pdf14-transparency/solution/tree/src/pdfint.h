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

typedef struct {
    long long off;      /* type 1: byte offset of "N G obj" */
    int   type;
    int   gen;
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

/* Standard-14 font metrics, defined in font_std14.c. Returns NULL when the
 * base font is not one of the 14. */
const short *pdf_std14_widths(const char *basefont, const char **encname);
int          pdf_std14_is(const char *basefont);

#endif /* PDFINT_H */
