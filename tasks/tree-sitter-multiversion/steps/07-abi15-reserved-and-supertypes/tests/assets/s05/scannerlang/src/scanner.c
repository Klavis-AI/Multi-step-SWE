// An external scanner that obtains its memory through the allocator header and
// keeps its state in the growable-array header, both of which `generate`
// places next to the parser header.
#include "tree_sitter/parser.h"
#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"

enum TokenType { BANG_RUN };

typedef struct {
  Array(char) seen;
} Scanner;

void *tree_sitter_s05scan_external_scanner_create(void) {
  Scanner *scanner = (Scanner *)ts_malloc(sizeof(Scanner));
  array_init(&scanner->seen);
  return scanner;
}

void tree_sitter_s05scan_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  array_delete(&scanner->seen);
  ts_free(scanner);
}

unsigned tree_sitter_s05scan_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  unsigned count = scanner->seen.size;
  if (count > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
    count = TREE_SITTER_SERIALIZATION_BUFFER_SIZE;
  }
  for (unsigned i = 0; i < count; i++) {
    buffer[i] = scanner->seen.contents[i];
  }
  return count;
}

void tree_sitter_s05scan_external_scanner_deserialize(void *payload, const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  array_clear(&scanner->seen);
  for (unsigned i = 0; i < length; i++) {
    array_push(&scanner->seen, buffer[i]);
  }
}

bool tree_sitter_s05scan_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  if (!valid_symbols[BANG_RUN]) {
    return false;
  }
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n' ||
         lexer->lookahead == '\r') {
    lexer->advance(lexer, true);
  }
  if (lexer->lookahead != '!') {
    return false;
  }
  while (lexer->lookahead == '!') {
    array_push(&scanner->seen, '!');
    lexer->advance(lexer, false);
  }
  lexer->result_symbol = BANG_RUN;
  return true;
}
