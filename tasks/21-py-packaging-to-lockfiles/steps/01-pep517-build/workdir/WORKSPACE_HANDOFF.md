# Workspace handoff

One entry per milestone, newest last. Keep it short and factual: what the tree
now is, and anything a later reader would otherwise have to rediscover by
reading a diff.

## Baseline — bento 0.2.0, as checked out

* Source layout: `bento/` and `bentomakerlib/` at the repository root, with
  vendored upstream copies under `bento/private/`.
* Build: `setup.py`, which monkey-patches distutils with Bento's own commands
  so that Bento builds Bento. `bootstrap.py` exists to bootstrap that circle.
* The distribution's metadata is declared in `bento.info`, in Bento's own
  format, and computed into distutils metadata at build time.
* The command line is `bentomaker`, implemented in `bentomakerlib/bentomaker.py`.
