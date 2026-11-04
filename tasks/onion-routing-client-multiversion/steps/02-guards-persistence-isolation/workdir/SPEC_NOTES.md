# Specification notes — step 02

First-party prose stating the normative protocol and design facts this step is graded against. Every fact is cited to a numbered section of the Tor specifications at commit `1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28) with a line-anchored URL. No specification body text is reproduced; the citations are to section headings and the prose below is written for this workspace.

Where a graded behaviour is an implementation choice rather than a protocol requirement — most of the persistence layer is — this document says so instead of reaching for an anchor that does not support the claim.

## Why a client keeps the same entry points

If a client chose a fresh entry relay for every circuit, an adversary running a small fraction of the network would eventually appear as the entry for some circuit, and from there could begin to correlate traffic. Choosing a small, slowly-changing set of entry relays — guards — and reusing them bounds that exposure: either the client picked a hostile guard at the start, or it did not.

**guard-spec.txt §1 (Introduction and motivation), L40:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L40

Guard selection is not a single choice but a layered one. The client keeps a *sample* drawn from the consensus, filters it to those it could actually use, records which ones it has successfully built through, and promotes a few of those to primary. Each layer exists to stop a different attack: sampling limits how much the network can steer the client by churning the consensus, and confirmation keeps the client from abandoning a working guard because of transient failure.

**guard-spec.txt §4.1 (The Sampled Guard Set), L210:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L210

**guard-spec.txt §4.2 (The Usable Sample), L312:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L312

**guard-spec.txt §4.3 (The confirmed-guard list), L360:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L360

**guard-spec.txt §4.4 (The Primary guards), L414:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L414

A guard that fails is not discarded. It is marked unreachable and retried later on a schedule, because a guard that is down for an hour is a worse reason to change entry points than the exposure a change would cause.

**guard-spec.txt §4.5 (Retrying guards), L445:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L445

**guard-spec.txt §4.6 (Selecting guards for circuits), L466:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L466

**guard-spec.txt §4.8 (When a circuit succeeds), L584:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L584

All of this is worthless if it is forgotten at shutdown. The guard state is exactly the kind of state that must outlive the process, because a client that re-sampled on every start would have no guards at all in any meaningful sense.

**guard-spec.txt §2 (State instances), L82:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L82

## What the specification does not decide: how that state is stored

The persistence layer graded here — a state directory, a lock held while a process owns it, a handle through which a component reads and writes its own slice of state, and a storage format that tolerates fields it does not recognise — is **not** specified by torspec. The specification requires that guard state persist; it says nothing about files, locks, or encodings.

Two consequences, both graded:

- Only one process may write the state at a time, and the loser of that race must be told, not silently allowed to corrupt it. Which is why acquiring the lock is fallible and its failure is a distinct, documented error rather than a panic.
- A state document written by a newer version must not destroy fields an older version does not understand, and vice versa. Forward-compatible storage is a design requirement here because the same directory outlives upgrades.

The directory-protocol analogue — that implementations must tolerate unrecognised material rather than reject it — is specified, and is the same principle applied one layer down:

**dir-spec.txt §3.8.1 (Forward compatibility), L3049:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3049

## Keeping unrelated activity apart

Two streams that should not be linkable must not share a circuit. A client therefore attaches a stream only to a circuit whose isolation is compatible with it, and otherwise builds a new one. What counts as "compatible" is a client policy — the protocol does not define isolation — but the reason it exists is the path-selection principle that a circuit is the unit an observer sees.

**path-spec.txt §3 (Attaching streams to circuits), L733:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L733

Isolation in this implementation is a token carried by a request, compared against the token a circuit was built for. Tokens compare equal only to themselves, so the default is separation and sharing must be asked for explicitly. That default direction is a design decision made here, and it is graded.

## Constraints on the relays that may share one path

**path-spec.txt §2.2 (Path selection and constraints), L303:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L303

Three of the constraints listed there are graded at this checkpoint. No relay may appear twice in one path — the sentence stating it is at
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L311 —
and no two relays in one path may sit in the same /16, unless that rule is explicitly switched off:
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L315

The subnet rule is about network position rather than declared ownership: two relays in one /16 are plausibly behind the same upstream, so treating them as independent overstates the path's diversity. Note that the rule as specified concerns IPv4 /16s, and that turning it off is a configuration option rather than a protocol variation.

## Parameters the network supplies

Tuning values arrive on the consensus "params" line rather than being compiled in, so the network can be retuned without new clients. A client reads a value when present, falls back to its own default when absent, and clamps out-of-range values instead of trusting them.

**dir-spec.txt §3.4.1 (Vote and consensus status document formats) — the "params" line, L2085:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2085

**param-spec.txt §1 (Network protocol parameters), L22:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L22

## Name lookup over SOCKS, and the cells that carry it

A client that resolved hostnames locally would leak its browsing to its own DNS resolver, defeating the point. Tor therefore carries name lookup inside the tunnel, exposed to applications as a SOCKS command extension rather than as ordinary DNS.

**socks-extensions.txt §2 (Name lookup), L63:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L63

**socks-extensions.txt §1.1 (Extent of support), L31:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L31

On the wire the lookup is a RESOLVE relay cell answered by a RESOLVED relay cell. The answer is a list of typed, individually time-to-live'd values rather than a single address, so a client must be able to parse several answers of different types and must not assume the first is the only one.

**tor-spec.txt §6.4 (Remote hostname lookup), L1960:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1960

Data on an open stream travels in RELAY_DATA cells, and the stream-opening and data paths are the same machinery the resolve path reuses.

**tor-spec.txt §6.2 (Opening streams and transferring data), L1756:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1756

**tor-spec.txt §6.1 (Relay cells), L1647:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1647

## What this checkpoint decides for itself

Graded, and **not** derived from any specification:

- The state-directory layout, the lock discipline, and the handle API through which a component reads and writes its own state.
- The isolation token type and the rule that distinct tokens never share a circuit.
- The shape of the configuration objects and their builders, and which fields carry defaults.
- The local-address and unit-scaling helpers, which are internal conveniences with no protocol meaning.
- Per scope decision S1, nothing in this step contacts the live Tor network at grading time; every behaviour above is observed against in-process fixtures.
