/* dom.h -- HTML document tree produced by the front end.
 *
 * Everything (nodes, tag names, text, attribute names and values) is
 * allocated from the arena passed to the parse entry point and stays valid
 * until arena_free() is called on it.
 */
#ifndef FRONTEND_DOM_H
#define FRONTEND_DOM_H

#include "util.h"

enum { DOM_ELEMENT = 1, DOM_TEXT = 2 };

typedef struct dom_attr { char *name; char *value; struct dom_attr *next; } dom_attr;

typedef struct dom_node {
  int type;
  char *tag;                    /* lowercased; NULL for text nodes */
  char *text;                   /* text content; NULL for elements */
  dom_attr *attrs;
  struct dom_node *parent, *first_child, *last_child, *prev_sibling, *next_sibling;
  int child_index;              /* 0-based among all children */
  int elem_index;               /* 0-based among element siblings, -1 for text */
  int n_elem_siblings;          /* number of element children of the parent */
  int doc_order;                /* preorder index over the whole document */
  void *style;                  /* left NULL: the cascade fills this in */
  void *box;                    /* left NULL: layout fills this in */
} dom_node;

typedef struct { dom_node *root; dom_node *html, *head, *body; arena *a; } dom_doc;

/* Parse HTML.  dom_parse_string never fails: malformed input produces a
 * best-effort tree, never NULL and never a crash.  dom_parse_file returns
 * NULL only when the file cannot be read; in that case, if `err` is non-NULL,
 * *err receives an arena-allocated message.  On success *err is set to NULL.
 *
 * doc->root is the <html> element (doc->root == doc->html always); doc->head
 * and doc->body are always present, synthesised if the source omitted them. */
dom_doc *dom_parse_file(arena *a, const char *path, char **err);
dom_doc *dom_parse_string(arena *a, const char *html, char **err);

const char *dom_attr_get(const dom_node *n, const char *name);  /* case-insensitive name */

#endif /* FRONTEND_DOM_H */
