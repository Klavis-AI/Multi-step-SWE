#include <tree_sitter/parser.h>

enum TokenType {
  AT_COLUMN_THREE,
};

void *tree_sitter_colx_external_scanner_create(void) {
  return NULL;
}

void tree_sitter_colx_external_scanner_destroy(void *payload) {
}

unsigned tree_sitter_colx_external_scanner_serialize(void *payload, char *buffer) {
  return 0;
}

void tree_sitter_colx_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
}

bool tree_sitter_colx_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  if (!valid_symbols[AT_COLUMN_THREE]) return false;
  if (lexer->lookahead != '@') return false;
  if (lexer->get_column(lexer) != 3) return false;
  lexer->advance(lexer, false);
  lexer->result_symbol = AT_COLUMN_THREE;
  return true;
}
