#ifndef ENGINE_H
#define ENGINE_H
#include <stddef.h>
typedef struct eng_doc eng_doc;
typedef struct eng_node eng_node;
/* Load and lay out a document from a filesystem path at the given viewport size.
   Returns NULL if the file cannot be read. */
eng_doc *eng_load_file(const char *path, double viewport_w, double viewport_h);
void eng_free(eng_doc *d);
/* Elements matching `selector` in document order. If `root` is non-NULL, only
   descendants of root. Writes up to `max` handles into out[].
   Returns the number written, or -1 if the selector is not valid CSS. */
int eng_query(eng_doc *d, const char *selector, eng_node *root, eng_node **out, int max);
/* Border-box rect in CSS pixels, relative to the initial containing block origin. */
void eng_rect(eng_doc *d, eng_node *n, double *x, double *y, double *w, double *h);
/* Resolved value of `prop`. Writes a NUL-terminated string into buf.
   Returns 0 on success, -1 if the property name is not supported (write ""). */
int eng_css(eng_doc *d, eng_node *n, const char *prop, char *buf, size_t buflen);
#endif
