/* pdfaint.h -- private contract for the PDF/A and PDF/UA conformance modules.
 *
 * Not installed and not referenced by pdf.h: pdfint.h and fontint.h are the
 * existing precedent for a module declaring its own API this way.  Everything
 * here allocates out of the document arena, so nothing needs a destructor;
 * pdf_close() reclaims it all.
 *
 * The two consumers are src/tools/verapdf.c (validation) and src/tools/gs.c
 * (conversion).  Both drive the same model so that what the checker READS and
 * what the converter WRITES cannot drift apart.
 */
#ifndef PDFAINT_H
#define PDFAINT_H

#include "pdfint.h"


/* ------------------------------------------------------------- flavours --- */

/* A flavour names a standard, a part and a level.  `level` is the conformance
 * letter as an ASCII character ('A', 'B', 'U', 'E', 'F') or 0 where the part
 * defines none -- ISO 19005-4's plain `4`, and ISO 14289-1, which has one
 * level and spells it nowhere. */
typedef struct {
    const char *name;   /* exactly as it is spelled on the command line   */
    int         std;    /* 19005 or 14289                                 */
    int         part;   /* 1..4 for ISO 19005; 1 for ISO 14289            */
    int         level;  /* 'A' | 'B' | 'U' | 'E' | 'F' | 0                */
} pdfa_flavour;

/* NULL when `name` is not a flavour this build knows.  The caller turns that
 * into the interface's exit status 2. */
const pdfa_flavour *pdfa_flavour_lookup(const char *name);

/* ------------------------------------------------------------ verdicts ---- */

#define PDFA_PASS       0   /* conforms at the requested flavour            */
#define PDFA_FAIL       1   /* parsed, does not conform                     */
#define PDFA_UNPARSABLE 2   /* not a PDF this checker can build a model of  */

/* Validate one already-open document at one flavour.  Returns PDFA_PASS or
 * PDFA_FAIL; it never returns PDFA_UNPARSABLE, because deciding that a file
 * cannot be parsed happens before a document exists -- see pdfa_parsable(). */
int pdfa_validate(pdf_doc *doc, const pdfa_flavour *fl);

/* The clause that refused the last document validated, for diagnostics on
 * standard error.  Never part of a verdict: --format text carries no clause
 * and no scored comparison reads one. */
const char *pdfa_last_reason(void);

/* Can this path be parsed into a document model at all?
 *
 * This is a DIFFERENT question from "does pdf_open() succeed".  The earlier
 * milestones require the reader to recover from a damaged cross-reference
 * table, and it does; a conformance checker asked about such a file still has a
 * model to judge and answers FAIL, not "unparsable".  What makes a file
 * unparsable is having no document structure to recover: no header, or no
 * trailer and no startxref anywhere in the file, so that nothing names a
 * catalogue.  Returns 1 when a model can be built, 0 when it cannot.
 *
 * `doc` may be NULL, in which case only the bytes are consulted. */
int pdfa_parsable(const char *path);

/* --------------------------------------------------------------- XMP ------ */

/* The catalogue's /Metadata packet, flattened into the handful of properties
 * every part's identification and provenance clauses actually read.  A packet
 * may spell a property as an attribute (pdfaid:part="1") or as an element
 * (<pdfaid:part>1</pdfaid:part>); both are accepted and normalised here. */
typedef struct {
    int   present;        /* a /Metadata stream was found and read          */
    int   filtered;       /* ... and it carried a /Filter, which 6.7.2 bans */
    int   wellformed;     /* the packet parsed as XML                       */

    int   have_part;      /* pdfaid:part was present                        */
    long  part;
    int   have_rev;       /* pdfaid:rev (ISO 19005-4 only)                  */
    long  rev;
    char  conformance[8]; /* pdfaid:conformance, "" when absent             */

    int   have_ua_part;   /* pdfuaid:part (ISO 14289-1)                     */
    long  ua_part;

    /* The properties the document information dictionary must agree with
     * (19005-1 6.7.3 / 19005-2 6.6.2).  "" means the property is absent,
     * which is not by itself a defect -- only disagreement is. */
    char  title[512], creator[512], description[512], keywords[512];
    char  creatortool[512], producer[512], createdate[128], moddate[128];
} pdfa_xmp;

/* Fills *out from the catalogue's /Metadata.  Returns 0 when there is no
 * metadata stream at all, which several clauses treat as their own failure. */
int pdfa_xmp_read(pdf_doc *doc, pdfa_xmp *out);

/* One property out of a raw XMP packet, by qualified name ("pdfaid:part").
 * Returns 0 when the property is absent.  Exposed because the converter needs
 * to carry a packet's properties forward into the one it writes. */
int pdfa_xmp_property(const char *packet, size_t len, const char *qname,
                      char *out, size_t outsz);

#endif /* PDFAINT_H */
