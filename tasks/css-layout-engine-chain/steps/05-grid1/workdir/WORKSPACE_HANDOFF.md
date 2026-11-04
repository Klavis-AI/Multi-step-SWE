# Workspace handoff

`/app` contains the remote end you are extending:

* `Makefile` — builds `build/cssdriver`. `make` must work from a clean copy.
* `src/` — the engine: HTTP and JSON, an HTML tree builder, a CSS tokenizer and
  cascade, computed values, and block and inline layout.
* `SPEC/` — what this milestone measures.
* `TEST_INTERFACE.md` — the WebDriver contract the remote end serves.
* `timer.sh` — prints the time remaining for this step.

`/app/css-corpus/` holds a practice corpus with its own recorded expectations and
a runner. It is a sample, not the whole of what the engine must handle:
passing it is not the same as being done, and the engine is expected to lay out
other documents of the same kinds just as well.

Everything the previous milestone made work is still required. The engine is
rebuilt from source in a clean copy of `/app`, so anything not committed to
`src/` and the `Makefile` does not exist for that build.
