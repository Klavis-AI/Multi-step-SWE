# Cython: tokenised, never parsed. ``cdef`` would stop ``ast`` dead, so a
# finding here proves the tokenizer path is the one that ran.

cdef int WIDTH = 8

LABEL = "[\$,%]"
