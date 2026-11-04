#include <tree_sitter/parser.h>
#include <string.h>

enum TokenType { HEREDOC_START, HEREDOC_BODY };

#define TAG_MAX 16

typedef struct {
  unsigned char tag_len;
  char tag[TAG_MAX];
} Scanner;

void *tree_sitter_s9scan_external_scanner_create(void) {
  static Scanner scanner;
  memset(&scanner, 0, sizeof(scanner));
  return &scanner;
}

void tree_sitter_s9scan_external_scanner_destroy(void *payload) { (void)payload; }

unsigned tree_sitter_s9scan_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *s = (Scanner *)payload;
  buffer[0] = (char)s->tag_len;
  memcpy(buffer + 1, s->tag, s->tag_len);
  return (unsigned)(1 + s->tag_len);
}

void tree_sitter_s9scan_external_scanner_deserialize(void *payload, const char *buffer,
                                                       unsigned length) {
  Scanner *s = (Scanner *)payload;
  s->tag_len = 0;
  memset(s->tag, 0, TAG_MAX);
  if (length > 0) {
    unsigned n = (unsigned char)buffer[0];
    if (n > TAG_MAX) n = TAG_MAX;
    if (n + 1 > length) n = (length > 0) ? length - 1 : 0;
    s->tag_len = (unsigned char)n;
    memcpy(s->tag, buffer + 1, n);
  }
}

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

bool tree_sitter_s9scan_external_scanner_scan(void *payload, TSLexer *lexer,
                                                const bool *valid_symbols) {
  Scanner *s = (Scanner *)payload;

  if (valid_symbols[HEREDOC_BODY] && s->tag_len > 0) {
    /* Consume everything up to and including a line that is exactly the tag. */
    unsigned matched = 0;
    bool at_line_start = true;
    while (lexer->lookahead != 0) {
      if (at_line_start && matched == 0) {
        lexer->mark_end(lexer);
      }
      if (at_line_start && matched < s->tag_len &&
          lexer->lookahead == (int32_t)(unsigned char)s->tag[matched]) {
        matched++;
        advance(lexer);
        if (matched == s->tag_len) {
          lexer->mark_end(lexer);
          lexer->result_symbol = HEREDOC_BODY;
          s->tag_len = 0;
          return true;
        }
        continue;
      }
      matched = 0;
      at_line_start = (lexer->lookahead == '\n');
      advance(lexer);
    }
    return false;
  }

  if (valid_symbols[HEREDOC_START]) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n') {
      skip(lexer);
    }
    if (lexer->lookahead != '<') return false;
    advance(lexer);
    if (lexer->lookahead != '<') return false;
    advance(lexer);
    unsigned n = 0;
    while (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z' && n < TAG_MAX) {
      s->tag[n++] = (char)lexer->lookahead;
      advance(lexer);
    }
    if (n == 0) return false;
    s->tag_len = (unsigned char)n;
    lexer->mark_end(lexer);
    lexer->result_symbol = HEREDOC_START;
    return true;
  }

  return false;
}
