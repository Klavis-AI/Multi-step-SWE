# Specification notes — step 09 (`09-fs-trust-and-dir-tolerance`)

First-party notes on the normative facts this step is graded against. Every
statement below is my own summary of a numbered section of the Tor
specifications; no specification text is reproduced. Read the cited section if
you need the full wording.

All citations are against torspec at the pinned commit
`1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28). The URL pattern is

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

Sections 1 to 8 summarise specification text and carry line anchors. Sections
9 to 11 describe behaviour the specifications do not mandate at all —
filesystem trust, configuration loading, safe logging — and say so explicitly,
because a reader who goes hunting for a citation there deserves to be told
there is none rather than left to conclude the notes are incomplete. Section
12 maps the whole lot onto the graded interface.

---

## 1. The three validity times of a consensus

**dir-spec.txt S1.4 ("Voting timeline"), L378:**
[dir-spec.txt#L378](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L378)

Every consensus document carries three instants: valid-after (VA), fresh-until
(FU) and valid-until (VU). The ordering is mandatory — VA must come before FU,
and FU before VU —
[dir-spec.txt#L380](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L380).
That single sentence is the whole justification for a constructor that can
fail: a three-timestamp value with the timestamps in the wrong order is not a
lifetime, so the type refuses to exist rather than being built and checked
later.

The authorities choose the three times so that each consensus stays fresh
until its successor becomes valid, and stays valid for a while after that; at
least three consensuses are expected to be valid simultaneously —
[dir-spec.txt#L384](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L384).
The overlap is deliberate: it is what lets a client with a slightly wrong
clock, or a slow download, still hold something usable.

The timeline proper runs as follows. At VA every authority holds the
multiply-signed consensus —
[dir-spec.txt#L418](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L418).
Caches fetch it between VA and FU; clients fetch it between FU and VU —
[dir-spec.txt#L428](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L428).
At VU the document stops being valid and a client that has not already
replaced it should keep trying —
[dir-spec.txt#L432](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L432).
Twenty-four hours past VU the client is told to stop using the document
entirely —
[dir-spec.txt#L435](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L435).
The interval lengths themselves have floors: FU minus VA and VU minus FU must
each be at least five minutes —
[dir-spec.txt#L437](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L437).

The practical reading for this step: VU is not a cliff. There is a documented
grace period after it, and the size of that grace period is a client policy
knob rather than a constant the specification fixes. That knob is what
`DirSkewTolerance` is.

---

## 2. What the document format says about each time

**dir-spec.txt S3.4.1 ("Vote and consensus status document formats"), L1865:**
[dir-spec.txt#L1865](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1865)

The three times appear once each, as `YYYY-MM-DD SP HH:MM:SS` in UTC — the
"valid-after" line, L1924
([dir-spec.txt#L1924](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1924)),
the "fresh-until" line, L1938
([dir-spec.txt#L1938](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1938)),
and the "valid-until" line, L1946
([dir-spec.txt#L1946](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1946)).

Two remarks in that section matter more to this step than the grammar does.

**Early arrival is expected.** Before VA the consensus derived from a vote is
not officially in use —
[dir-spec.txt#L1928](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1928)
— but propagation delays mean clients and relays routinely see documents up to
`DistSeconds` before their own VA, and the specification says outright that
they should not complain about it —
[dir-spec.txt#L1932](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1932).
This is the normative basis for a *pre*-validity tolerance: a document whose
VA is slightly in the future is not evidence of an attack, and rejecting it
outright is a bug, not caution.

**Late use is expected too.** FU is described as the moment the next consensus
should exist, so there is no point fetching before it —
[dir-spec.txt#L1942](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1942)
— and VU as the moment after which every client should look for something more
recent —
[dir-spec.txt#L1950](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1950).
The section then records what implementations actually do: clients keep using
a consensus for up to twenty-four hours past VU when nothing fresher can be
fetched —
[dir-spec.txt#L1954](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1954).
So the *post*-validity tolerance is not an arti invention either; only its
magnitude is a local choice.

---

## 3. Liveness, and what a client does without it

**dir-spec.txt S5.1 ("Downloading network-status documents"), L3685:**
[dir-spec.txt#L3685](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3685)

A client aims to hold a *live* consensus at all times, "live" being defined
purely by the clock: VA has passed and VU has not —
[dir-spec.txt#L3696](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3696).
Note what the definition does *not* mention: nothing about FU, and nothing
about how the document was obtained. Liveness is a predicate on two
timestamps, which is exactly why a wrong local clock turns a perfectly good
network into an unusable one.

With no consensus at all, the client bootstraps from a randomly chosen
fallback mirror or authority, preferring fallbacks —
[dir-spec.txt#L3700](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3700)
— and must discard any network-status document it did not ask for —
[dir-spec.txt#L3707](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3707).
Replacement downloads are deliberately scattered in time so that the whole
client population does not converge on the caches the instant a consensus
expires —
[dir-spec.txt#L3737](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3737).

The sentence that this step's tolerance model is built on comes at the end of
the section: when no live consensus is available, a client will generally fall
back on the most recent one it has, provided that one is "reasonably live",
meaning it expired less than twenty-four hours ago —
[dir-spec.txt#L3756](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3756).

"Reasonably live" is therefore a second, looser liveness predicate layered on
the first. A directory manager that only implements the strict one cannot
survive an outage; one that implements both needs the width of the loose
window to be a configurable quantity. In arti that quantity is a pair — a
tolerance before VA and a tolerance after VU — rather than the single constant
C Tor uses, because the two directions are justified by different paragraphs
and have different risks.

---

## 4. Why the post-validity window is days rather than hours

**Proposal 212, "Increase Acceptable Consensus Age"** —
[proposals/212-using-old-consensus.txt](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/proposals/212-using-old-consensus.txt)

(No line anchor: the proposals subdirectory is outside the flat corpus the
anchor checker indexes, so a `#L` reference here could not be verified and I
will not write one I cannot stand behind.)

The proposal's argument, in my own words: if the authorities fail to produce a
consensus for twenty-four hours, every client on the network simultaneously
loses its directory information and tries to re-bootstrap directly from the
authorities — which turns an outage into a self-inflicted denial of service
against the very machines that need to recover. A deliberate attack timed to a
long public holiday would be unlikely to be fixed inside the window. The
proposal therefore argues for raising the consensus liveness constant from one
day to three, explicitly framed as a compromise between surviving a long
weekend and letting a hostile mirror pin a client to a stale view forever.

It also records the mitigating rule that makes three days tolerable: a client
must not simply settle for old data, it must keep trying to fetch something
fresher in the background. An expired-but-tolerated consensus is a fallback
for continuing to operate, never a reason to stop reaching for a current one.

This is why the graded default for the post-validity tolerance is three days
and not the twenty-four hours that section 3 quotes. The specification records
what C Tor did; the proposal records the reasoning for the larger number that
arti adopts.

---

## 5. Authority certificates: skew is anticipated, not excused

**dir-spec.txt S3.1 ("Creating key certificates"), L1510:**
[dir-spec.txt#L1510](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1510)

An authority key certificate carries a publication time and an expiry time.
Implementations should reject a certificate published too far in the future,
but may tolerate some clock skew —
[dir-spec.txt#L1550](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1550)
— and should reject an expired certificate, again with explicit permission to
tolerate some skew —
[dir-spec.txt#L1559](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1559).

Both sentences pair a SHOULD-reject with a MAY-tolerate. That shape is the
model for the whole tolerance design in this step: the default answer to an
untimely document is "no", the tolerance is a named, bounded, configurable
relaxation of that "no", and the relaxation is symmetric in principle even
when the configured widths differ.

---

## 6. What makes a consensus *invalid* as opposed to untimely

Timeliness and validity are separate failures, and a directory manager that
conflates them will retry the wrong thing.

The trust model has been majority-based since the version-3 protocol was
designed: a client believes a statement about a relay when more than half of
the authorities attest to it — dir-spec.txt S0.1 ("History"),
[dir-spec.txt#L141](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L141).

Concretely, a consensus carries one `directory-signature` item per signing
authority — dir-spec.txt S3.4.1,
[dir-spec.txt#L2521](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2521).
An item whose algorithm the implementation does not recognise must be ignored
rather than treated as a failure —
[dir-spec.txt#L2534](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2534)
— and at most one signature from any given authority counts towards the total
—
[dir-spec.txt#L2541](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2541).

So "not enough signatures" is a property of the document that no amount of
waiting will change, whereas "not yet valid" is a property of the pair
(document, clock) that may resolve itself in seconds. A tolerant directory
manager keeps these apart: it may accept an untimely document under a
configured allowance, but it never widens an allowance to admit an
insufficiently signed one.

---

## 7. The bootstrap set a client must know before it knows anything

**dir-spec.txt S5.1, L3685** (same section as note 3):
[dir-spec.txt#L3687](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3687)

Each client maintains a list of directory authorities and, insofar as
possible, every client uses the same list. Newer versions additionally ship a
list of default fallback directory mirrors, which may differ from release to
release —
[dir-spec.txt#L3691](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3691).

Both lists must be compiled in, because they are the only things a client
knows before it has a consensus. That is a configuration requirement as much
as a protocol one: the directory configuration must supply non-empty defaults
for both, and an embedder must be able to override them without being able to
accidentally empty them.

Circuits, meanwhile, are not to be built until the client has enough directory
information — dir-spec.txt S5.4.1 ("Choosing routers for circuits."),
[dir-spec.txt#L3853](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3853)
— and only relays listed in the consensus may be used —
[dir-spec.txt#L3857](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3857).

---

## 8. Retrying, and why tolerance is not a substitute for it

**dir-spec.txt S5.5 ("Retrying failed downloads"), L3890:**
[dir-spec.txt#L3890](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890)

A failed download of any directory resource is followed by a wait computed
with a randomised exponential backoff — a "decorrelated jitter" scheme —
before the next attempt —
[dir-spec.txt#L3894](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3894).
The recurrence triples the previous delay to form the upper bound of a uniform
draw, clamped by a cap —
[dir-spec.txt#L3904](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3904).

This is inherited machinery from an earlier step, restated here for one
reason: tolerating an old consensus and retrying for a new one are meant to
run concurrently. The tolerance keeps the client working; the retry schedule
is what eventually makes it correct again. A directory manager that stops
retrying because the stale document is still inside its tolerance window has
implemented half of proposal 212 and the dangerous half at that.

---

## 9. Filesystem permission checking — not specified

The Tor specifications say nothing about the permissions of the files a client
keeps on disk. There is no citation to give for this section and none is
implied below; everything here is arti's own security model, and it is graded
because the conformance driver exercises it, not because torspec mandates it.

The threat is straightforward. A client's state directory holds guard
selections, and its cache directory holds the directory documents that decide
which relays exist. An attacker who can write either one can choose the
client's guards or feed it a curated view of the network, without ever
touching the wire. So before either directory is used, every component of the
path to it is inspected.

The rules arti adopts, and the reasoning behind each:

- **The object you asked about must not be reachable by anybody else at all.**
  No group or other permission bit of any kind. Read access is included
  because the contents are themselves sensitive.
- **An ancestor directory only has to be un-writable by others.** A
  world-readable, world-searchable parent leaks nothing that matters; a
  world-*writable* one lets an attacker replace the subtree beneath it.
- **A world-writable ancestor with the sticky bit set is tolerated.** That is
  precisely the `/tmp` arrangement: with the sticky bit, only an entry's owner
  may rename or remove it, so the write bit no longer implies control of the
  children. The exemption applies to intermediate directories only — a final
  object is judged on its own access bits regardless of sticky.
- **The owner must be trusted.** Ownership by root is always acceptable, since
  root could defeat any check anyway; ownership by anyone else must be
  explicitly trusted. A wrong owner is a different error from wrong bits,
  because the remedy is different.
- **A named trusted group excuses that group's bits, and only for objects the
  group owns.** Trusting a group by name or id is how a shared-service
  deployment is meant to work; trusting it should not silently relax anything
  for objects owned by some other group.
- **Content below a checked directory is judged by the ancestor rule.** A
  world-readable file in a checked state directory is a privacy question for
  its author; a world-writable one is a live tampering vector for the client.

Two escape hatches exist because the rules are strict enough to be wrong on
some hosts: an ignored prefix, which stops the upward walk at a directory the
operator vouches for, and a blanket "trust everyone" mode. The blanket mode is
what the deployment environment variable `ARTI_FS_DISABLE_PERMISSION_CHECKS`
ultimately selects; it exists so that a container or a test harness can opt out
deliberately rather than by accident. Because it is an opt-out of last resort
and not a tunable, it is a **presence** test and not a value test: the variable
being set to anything at all - including `0` and the empty string - disables
the checks, and only an unset variable leaves them on. A configuration that
parsed the value would silently re-enable checking for an operator who wrote
`ARTI_FS_DISABLE_PERMISSION_CHECKS=0` meaning "yes, disable them".

**Migration.** At this step a world-writable state directory becomes a startup
error rather than a warning. That is a deliberate break: silently continuing
with a compromised state directory is worse than refusing to start, because
the user never learns that their guard choices were never theirs.

---

## 10. Configuration sources and resolution — not specified

Also outside the specifications, and also graded. No citation applies.

Arti's configuration is assembled from a command line that offers three
things: one *default* configuration file path, zero or more files named with
`-c`/`--config-file`, and individual `key=value` overrides given with
`-o`/`--option`. Four properties are worth stating because they are observable
and easy to get subtly wrong.

- **The named files replace the default; they are not added to it.** The
  default path is a *fallback*, consulted only when the user named no file at
  all. If even one `-c` is given, the default file is never read, not even
  first and not even as a lower-precedence layer — a user who overrides the
  configuration location expects the shipped defaults to be out of the picture,
  not silently underneath. The two cases also differ in strictness: a file the
  user named must exist, while the default path is consulted optionally and its
  absence is fine.
- **Order is precedence.** Files are applied in the order they were added, so
  a later file overrides an earlier one. Command-line overrides are applied
  after every file no matter when they were pushed onto the list, because an
  argument the user typed at the prompt should never lose to a file on disk.
- **Absence and unreadability are different.** A file explicitly named by the
  user must exist; a file that is merely the default location need not. But an
  optional file that *does* exist and has unsafe permissions is still an
  error — being optional excuses absence, not untrustworthiness. Configuration
  files are permitted to be world-readable, since they are not secrets, and
  refused when world-writable, since anyone could then rewrite the client's
  behaviour.
- **Unrecognised keys are reported, not ignored.** Several independent
  consumers resolve their own configuration from one shared tree, each
  deserialising the whole document and keeping what it recognises. A key is
  reported to the user only when *no* consumer claimed it; the report is the
  intersection across consumers, computed at the deepest point any consumer
  descended to. This is what makes a typo in a config file diagnosable instead
  of silent, and it is why the resolution machinery cannot be replaced with
  serde's flattening.

---

## 11. Safe logging — not specified

Inherited from step 08 and restated for completeness: a wrapper type renders
as a fixed placeholder under every formatting trait, with a scoped suppression
that reveals the value inside a closure and restores scrubbing afterwards.
Nothing in the specifications requires this; it exists so that a log file
shipped to a bug tracker does not carry the addresses its author visited.

---

## 12. How these facts reach the graded interface

Not every note above is *graded* at this step, and the difference matters: a
note in the second group is background you should understand, not a behaviour
the driver will check. The third column says which is which, and where a note
is not graded it says why, so that neither an implementer nor a later reviewer
has to guess.

| Note | What it pins | Graded at step 09? |
|---|---|---|
| 1, 2 | Three ordered validity times; a constructor that rejects the wrong order | **Yes** — `lifetimebase` |
| 2, 3, 4 | A tolerance before valid-after and a wider one after valid-until; three days as the post-validity default | **Yes** — `skewtol`, `dirmgrcfg` (the *values* and their plumbing) |
| 3, 7 | Non-empty compiled-in authority and fallback lists in the directory configuration | **Yes** — `dirmgrcfg` |
| 5 | Untimeliness is a bounded, configurable relaxation of a default refusal | **Yes** — `skewtol`. Both halves are graded: the widths themselves, and the relaxation they perform, on the public surface the release performs it with. `TimerangeBound::extend_tolerance`/`extend_pre_tolerance` admit an expired or not-yet-valid object once the matching width is applied, still refuse one outside the widened window, and leave the opposite bound untouched; a `netstatus::Lifetime` rebuilt with `valid_after - pre` and `valid_until + post` is valid at instants the strict window rejects, and its `fresh_until` does not move. Still out of scope: `DirSkewTolerance`'s own `extend_tolerance`/`extend_lifetime`, which are `pub(crate)` at this tag and cannot be named from a driver, and time-checking a consensus a driver actually downloaded and parsed. |
| 6 | Signature sufficiency is not a timing question and no tolerance widens it | **No — out of scope here.** Consensus signature checking is not on this step's public surface; `TEST_INTERFACE.md` names no item through which a driver could distinguish "invalid" from "untimely". The note is background for why the tolerance knobs must not be allowed to leak into validation. |
| 8 | Tolerance runs alongside retrying, never instead of it | **No — out of scope here.** The retry schedule (`DownloadScheduleConfig`) is only constructed, not exercised; driving an actual download schedule needs a running `DirMgr` and a network, neither of which this step's driver has. |
| 9 | The whole `fs-mistrust` surface and the state-manager startup error | **Yes** — `mistrustcfg`, `trustedid`, `mistrusterr`, `pathtolerance`, `checkeddir`, `statemgrperm` |
| 9 | The blanket escape hatch reached from the environment | **Yes** — `storagecfg`, via `arti_client::config::fs_permissions_checks_disabled_via_env()` |
| 10 | Configuration sources, permission checks on config files, key resolution | **Yes** — `cfgsources`, `cfgresolve`, `cmdlineopts` |
| 11 | The inherited safe-logging wrapper, and its scoped suppression **revealing** the value | **Yes** — `safelogbase` |
