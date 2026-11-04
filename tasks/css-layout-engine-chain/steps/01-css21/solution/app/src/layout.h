#ifndef LAYOUT_H
#define LAYOUT_H
#include "style.h"
typedef struct lay_box lay_box;
struct lay_box {
  double x, y, w, h;                 /* border box, absolute, from the ICB origin */
  double content_w, content_h;
  double scroll_w, scroll_h;
  /* Used values, indexed by the EDGE_* constants of style.h.  `margin` holds
     the value the used-width/height equations solved for, so 'auto' margins
     read back as the length they resolved to (§10.3, §10.6).  `inset` holds
     the used offsets of a positioned box (§9.4.3, §10.3.7, §10.6.4) and is 0
     for a static one, whose offsets have no used value. */
  double margin[4], padding[4], border[4];
  double inset[4];
  const computed *st; void *node;
  lay_box *parent, *first_child, *last_child, *prev, *next;
};
typedef struct lay_doc lay_doc;
typedef struct {
  void *root;
  void *(*first_child)(void *node);
  void *(*next_sibling)(void *node);
  const computed *(*style_of)(void *node);   /* NULL for text nodes */
  const char *(*text_of)(void *node);        /* NULL for elements */
  /* Font metrics are a function of the whole computed font, not of the size
     alone: two elements at one size in different families have different
     advances and different half-leading.  `font_line_gap` is what separates
     'line-height: normal' from the content height (CSS 2.1 10.8.1). */
  double (*text_width)(const char *text, const computed *st);
  double (*font_ascent)(const computed *st);
  double (*font_descent)(const computed *st);
  double (*font_line_gap)(const computed *st);
} lay_env;
lay_doc *lay_run(const lay_env *env, double viewport_w, double viewport_h);
lay_box *lay_box_for(lay_doc *d, void *node);
void     lay_free(lay_doc *d);
#endif
