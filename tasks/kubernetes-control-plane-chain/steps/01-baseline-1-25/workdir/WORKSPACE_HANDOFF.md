# Workspace handoff

A later session starts from this workspace with no memory of this one. Keep this
file current: what is built, where the sources are, which parts of the API are
implemented, which are stubbed, and what the next session should look at first.

## Layout

| path | what it is |
|---|---|
| `Makefile` | you write it; `make` must produce `/out/bin/{kube-apiserver,kube-controller-manager,kube-scheduler}` (outside `/app`, which stays text-only) |
| `SPEC/` | the release's published OpenAPI specification, the required API surface, and `TEST_INTERFACE.md` |
| `conformance/` | the conformance corpus and `run.sh` |
| `WORKSPACE_HANDOFF.md` | this file |

## Status

_(fill in)_
