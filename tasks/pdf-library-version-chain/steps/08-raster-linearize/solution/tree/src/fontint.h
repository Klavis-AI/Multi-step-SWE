/* fontint.h -- private contract shared by font_std14.c, cmap.c, font.c and
 * text.c.  Not installed, not referenced by pdf.h or pdfint.h.
 *
 * Everything here allocates out of the document arena, so nothing needs an
 * explicit destructor; pdf_close() reclaims it all.
 */
#ifndef FONTINT_H
#define FONTINT_H

#include "pdfint.h"

/* ======================================================== font_std14.c === */

/* Indices into the standard-14 table.  The four faces of a family are laid out
 * regular, bold, italic, bold-italic. */
#define PDF_STD14_COURIER    0
#define PDF_STD14_HELVETICA  4
#define PDF_STD14_TIMES      8
#define PDF_STD14_SYMBOL    12
#define PDF_STD14_ZAPF      13

/* -1 when the base font is not one of the 14 (aliases included). */
int         pdf_std14_index(const char *basefont);
const char *pdf_std14_canon(int idx);

/* Width of a glyph NAME in 1/1000 text-space units, or -1 when this face has
 * no such glyph.  Name-keyed so /Differences cannot invalidate it. */
int pdf_std14_width_name(int idx, const char *glyph);

/* 256 glyph names, "" where the code is unused.  NULL for an unknown name. */
const char *const *pdf_std14_builtin_enc(int idx);
const char *const *pdf_base_encoding(const char *encname);

/* Six upper-case letters plus '+' (ISO 32000-1 9.6.4); the same rule poppler
 * uses for its "sub" column. */
const char *pdf_subset_tag_skip(const char *basefont);
int         pdf_has_subset_tag(const char *basefont);

/* Glyph name to Unicode: the Adobe Glyph List, the ZapfDingbats list, and the
 * algorithmic uniXXXX / uXXXX[XX] / "f_f_i" / "one.oldstyle" forms.  Returns
 * the number of scalar values written, 0 when the name maps to nothing. */
int pdf_glyph_to_unicode(const char *glyph, unsigned int *out, int max);

/* Recognises gNN / GNN / cidNN / glyphNN / indexNN, which name a slot rather
 * than a character. */
int pdf_glyph_is_index(const char *glyph, unsigned int *num);

/* ============================================================= cmap.c ==== */

typedef struct pdf_cmap pdf_cmap;

pdf_cmap *pdf_cmap_identity(pdf_doc *doc, int vertical);
pdf_cmap *pdf_cmap_parse(pdf_doc *doc, const unsigned char *src, size_t len);
pdf_cmap *pdf_cmap_predefined(pdf_doc *doc, const char *name);

/* /UseCMap: the parent supplies whatever the child does not define. */
void        pdf_cmap_set_parent(pdf_cmap *cm, const pdf_cmap *parent);
const char *pdf_cmap_usecmap_name(const pdf_cmap *cm);

int         pdf_cmap_vertical(const pdf_cmap *cm);
int         pdf_cmap_exact(const pdf_cmap *cm);     /* 0 => guessed 2-byte identity */
const char *pdf_cmap_name(const pdf_cmap *cm);

/* Consume one code from a string using the codespace ranges (1..4 bytes).
 * Always makes progress: on no match it takes the shortest codespace length,
 * defaulting to one byte. */
int pdf_cmap_next(const pdf_cmap *cm, const unsigned char *s, size_t n,
                  unsigned int *code, int *nbytes);

/* -1 when the code has no cidrange/cidchar entry. */
int pdf_cmap_cid(const pdf_cmap *cm, unsigned int code);

/* bfchar/bfrange destinations; may be several scalars (ligatures). */
int pdf_cmap_unicode(const pdf_cmap *cm, unsigned int code,
                     unsigned int *out, int max);
int pdf_cmap_has_unicode(const pdf_cmap *cm);

/* ============================================================== font.c === */

typedef struct pdf_fontcache pdf_fontcache;

pdf_fontcache *pdf_fontcache_new(pdf_doc *doc);
pdf_font      *pdf_fontcache_get(pdf_fontcache *fc, pdf_obj *fontdict);

pdf_font *pdf_font_load(pdf_doc *doc, pdf_obj *fontdict);

/* Split the next code out of a shown string.  Simple fonts always take one
 * byte; composite fonts go through the encoding CMap. */
int pdf_font_next_code(const pdf_font *f, const unsigned char *s, size_t n,
                       unsigned int *code, int *nbytes);

/* Horizontal displacement w0 in 1/1000 text-space units (glyph-space units for
 * a Type 3 font, which text.c then puts through /FontMatrix). */
double pdf_font_width(const pdf_font *f, unsigned int code);

int pdf_font_unicode(const pdf_font *f, unsigned int code,
                     unsigned int *out, int max);

int           pdf_font_is_type3(const pdf_font *f);
int           pdf_font_vertical(const pdf_font *f);
const double *pdf_font_matrix(const pdf_font *f);   /* six entries */
pdf_obj      *pdf_font_t3_resources(const pdf_font *f);

/* Fill one pdffonts row from a font dictionary. */
void pdf_font_info(pdf_doc *doc, pdf_obj *fontdict, int num, int gen,
                   pdf_fontinfo *fi);

#endif /* FONTINT_H */
