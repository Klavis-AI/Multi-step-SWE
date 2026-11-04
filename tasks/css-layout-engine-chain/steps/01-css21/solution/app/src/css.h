/* css.h -- CSS Syntax Level 3 tokenizer and parser.
 *
 * All strings handed back live in the arena passed to the parse entry point.
 * The input text only needs to stay valid for the duration of the call.
 */
#ifndef FRONTEND_CSS_H
#define FRONTEND_CSS_H

#include "util.h"

typedef struct css_decl {
  char *prop;                    /* lowercased, e.g. "margin-top"; custom props keep case: "--Foo" */
  char *value;                   /* the declaration value, whitespace-trimmed, comments removed */
  int important;
  struct css_decl *next;
} css_decl;

enum { CSS_STYLE_RULE = 1, CSS_AT_RULE = 2 };

typedef struct css_rule {
  int kind;
  char *prelude;                 /* style rule: the full selector list text. at-rule: the prelude */
  char *at_name;                 /* at-rule only, lowercased without '@' (e.g. "media", "layer") */
  char **selectors;              /* style rule only: prelude split on top-level commas, trimmed */
  int   n_selectors;
  css_decl *decls;               /* declarations directly in this rule's block */
  struct css_rule *children;     /* nested rules inside the block (at-rules, and nested style rules) */
  struct css_rule *next;
  int order;                     /* 0-based index in document order across the whole sheet, preorder */
} css_rule;

typedef struct { css_rule *rules; } css_sheet;

css_sheet *css_parse_sheet(arena *a, const char *text);
css_decl  *css_parse_decls(arena *a, const char *text);   /* for a style="" attribute */

#endif /* FRONTEND_CSS_H */
