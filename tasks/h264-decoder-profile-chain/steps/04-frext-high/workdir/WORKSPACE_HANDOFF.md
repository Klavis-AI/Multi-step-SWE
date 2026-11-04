# Workspace handoff

`/app` is a cumulative workspace: it is carried forward unchanged into any
working session that follows this one. Such a session starts fresh with no
memory of this one and reads only this workspace and its own ticket. This
document is written the same way on every milestone, so write the handoff as if
a successor exists even when none does.

## What is here

| path | what it is |
| --- | --- |
| `/app/bin/ldecod` | the decoder executable you produce (see `/app/TEST_INTERFACE.md`) |
| `/app/TEST_INTERFACE.md` | how the decoder is invoked and what it must write. Constant. |
| `/app/SPEC/` | the normative requirement document for each milestone |
| `/app/tests/visible/` | practice bitstreams with expected per-frame digests |
| `/app/timer.sh` | advisory countdown for this session's budget |

## What to leave behind

Keep this file current. A later session benefits from knowing, in your own
words:

- where the decoder's source lives and how it is organised;
- which parts of `/app/SPEC/` are implemented and which are not;
- what `node tests/visible/run_visible.mjs` reports right now;
- anything you found surprising in the bitstreams or the specification.

Do not delete `/app/SPEC/`, `/app/TEST_INTERFACE.md`, or `/app/tests/`.

## Keep the workspace to source

Write decoded YUV and other build output to a temporary directory, not into
`/app`. Raw video is large — a handful of decoded practice streams outweighs
every source file here — and this workspace is carried forward whole into the
next session. `tests/visible/run_visible.mjs` already decodes to a temporary
directory for this reason.
