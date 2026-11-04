# WORKSPACE_HANDOFF.md

This file is the running record of this repository. Whoever works on it next may have
no memory of this session and will read this file first. Keep it accurate and short.

Update it before you finish: what the interpreter now implements, how it is laid out,
which parts are known-incomplete, and anything that would otherwise have to be
rediscovered.

## Layout

```
/app
  Makefile          `make` must build sapi/cli/php
  sapi/cli/         the CLI SAPI entry point
  tests/            php-src's test corpus, and run-phpt.py
  SPEC/             php-src's UPGRADING notes
  WORKSPACE_HANDOFF.md
  TEST_INTERFACE.md how the interpreter is built, started and driven
```

## State

(nothing yet)

## What is implemented

(fill in)

## What is not implemented

(fill in)

## How to check progress

```
make
python3 tests/run-phpt.py --php sapi/cli/php --root tests/phpt \
        tests/phpt/Zend/tests --show-diff 3
```
