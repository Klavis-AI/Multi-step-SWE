#include "tree_sitter/parser.h"

enum TokenType { PERCENT_STRING };

void *tree_sitter_ext_external_scanner_create(void) { return (void *)0; }

void tree_sitter_ext_external_scanner_destroy(void *payload) { (void)payload; }

unsigned tree_sitter_ext_external_scanner_serialize(void *payload, char *buffer) {
  (void)payload; (void)buffer; return 0;
}

void tree_sitter_ext_external_scanner_deserialize(void *payload, const char *buffer,
                                                  unsigned length) {
  (void)payload; (void)buffer; (void)length;
}

bool tree_sitter_ext_external_scanner_scan(void *payload, TSLexer *lexer,
                                           const bool *valid_symbols) {
  (void)payload;
  if (!valid_symbols[PERCENT_STRING]) return false;
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\n' || lexer->lookahead == '\r') {
    lexer->advance(lexer, true);
  }
  if (lexer->lookahead != '%') return false;
  lexer->advance(lexer, false);
  while (lexer->lookahead != 0 && lexer->lookahead != '%') {
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead != '%') return false;
  lexer->advance(lexer, false);
  lexer->result_symbol = PERCENT_STRING;
  return true;
}
