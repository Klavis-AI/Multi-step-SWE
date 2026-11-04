# Specification notes — step 07

First-party prose stating the normative protocol and design facts this step is graded against. Every fact is cited to a numbered section of the Tor specifications at commit `1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28) with a line-anchored URL. No specification body text is reproduced; the citations are to section headings and the prose below is written for this workspace.

A caution before the citations, because this checkpoint sits unusually close to the line between protocol and policy. Two of the things it builds — how a client decides that two streams must not share a circuit, and how it orders its attempts when a host has both IPv4 and IPv6 addresses — are almost entirely implementation choice. The spec fixes the *wire* consequences of name lookup and the *existence* of isolation as a SOCKS-visible concept, and leaves the mechanism to the client. Where that is so this document says so, rather than reaching for an anchor that does not support the claim. A citation means the spec constrains the behaviour; its absence means the client is free, and the tests grade this implementation's documented choice instead.

## Why a Tor client never asks the local resolver

The reason a client needs any of this machinery is that an ordinary DNS lookup would defeat the point of using Tor. Resolving `example.com` through the system resolver tells the local network, and the configured recursive resolver, exactly where the user is about to go — before a single byte has entered a circuit. So name lookup has to travel the same protected path the traffic will: the client sends the name *into* the network and lets an exit relay resolve it.

That is a protocol operation with its own relay cell pair, not a local one. The client sends `RELAY_RESOLVE` carrying the name; the exit answers with `RELAY_RESOLVED` carrying zero or more answers.

**tor-spec.txt §6.4 (Remote hostname lookup), L1960:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1960

Each answer in a `RELAY_RESOLVED` cell is a type/length/value/TTL record. Two of the type codes are not addresses at all but error indications, distinguished by whether a retry could succeed: one transient, one permanent. That distinction is the whole reason a resolution failure is not a single opaque error in this implementation — a caller that cannot tell "try again" from "this name will never resolve" either retries forever or gives up too early.

**tor-spec.txt §6.4, answer type codes, L1979:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1979

A resolution borrows a stream ID without creating a stream. The request must carry a nonzero stream ID distinct from any in use, the answer must come back on that same ID, and no stream exists at the exit as a result. This is why a resolution cannot simply be modelled as a short-lived connection: it occupies an ID in the circuit's namespace but has no data phase, no flow-control window, and no end cell.

**tor-spec.txt §6.4, stream ID requirement, L1996:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1996

Streams that *do* carry data are opened with a different cell and have the flow-control machinery a resolution lacks; the contrast is what fixes the shape of the resolution path.

**tor-spec.txt §6.2 (Opening streams and transferring data), L1756:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1756

## How an application asks for a lookup

A client exposes remote resolution to applications through its SOCKS port, using command values that are Tor extensions rather than part of SOCKS proper. A forward lookup uses a `RESOLVE` command and returns the address in the field a SOCKS reply reserves for a bound address.

**socks-extensions.txt §2 (Name lookup), L63:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L63

Reverse lookup is a separate command, and — unlike forward lookup, which is offered on both SOCKS dialects — it is SOCKS5 only.

**socks-extensions.txt §2, reverse resolution, L75:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L75

The same section states the forward command's dialect coverage.

**socks-extensions.txt §2, forward resolution command value, L66:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L66

Two omissions from the supported SOCKS surface matter to this checkpoint directly. SOCKS5's `UDP ASSOCIATE` is not supported, which is why a DNS *port* — a plain listener speaking the DNS wire format — exists as a separate front end rather than being folded into the SOCKS port. A client that wanted to serve ordinary resolver traffic could not do it over SOCKS even if it wished to.

**socks-extensions.txt §1.1 (Extent of support), UDP ASSOCIATE, L42:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L42

The second omission is the load-bearing one for isolation, and it is the only place torspec says anything normative about the subject at all: SOCKS4 usernames are given a meaning they do not have in SOCKS4, namely to separate streams from one another.

**socks-extensions.txt §1.1, usernames as an isolation signal, L39:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L39

## Keeping unrelated activity apart

Everything above is the whole of the specification's contribution to stream isolation: credentials presented at the SOCKS port can distinguish streams, and the client must honour that distinction. *How* it does so — what an isolation token is, when two tokens are compatible, how a token attaches to a circuit, whether isolation is a property of the request or of the circuit — is not specified. This implementation's answer is graded as a documented design choice, not as conformance, and `TEST_INTERFACE.md` is the authority for it.

What the spec does fix is the moment at which isolation has to have been decided. A stream is attached to a circuit that "might support" it, and the attaching step is where a request either finds a circuit it may share or causes a new one to be built. An isolation rule that were consulted after attachment would be too late to prevent the sharing it exists to prevent.

**path-spec.txt §3 (Attaching streams to circuits), L733:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L733

Attachment is also bounded in time: a request that stays unattached long enough is abandoned and the failure is reported back to the application. Isolation makes this reachable in a way it otherwise would not be — a request whose isolation admits no existing circuit waits for one to be built, and that wait is the wait this bound governs.

**path-spec.txt §3, unattached-request timeout, L742:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L742

Isolation is a restriction *added to* path selection, never a licence to relax it. The general constraints on which relays may occupy which positions continue to apply to every circuit an isolated request causes to be built.

**path-spec.txt §2.2 (Path selection and constraints), L303:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L303

Configuration can further narrow path selection, and those restrictions compose with isolation rather than overriding it.

**path-spec.txt §2.2.2 (User configuration), L400:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L400

## Backing off when a download fails

The retry machinery this step installs is one of the rare cases where an implementation strategy *is* named by the spec. Clients retry failed directory downloads on a randomised exponential backoff, and the spec identifies the specific variant — decorrelated jitter — and gives the recurrence for the delay sequence.

**dir-spec.txt §5.5 (Retrying failed downloads), L3890:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890

**dir-spec.txt §5.5, the named backoff algorithm, L3898:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3898

**dir-spec.txt §5.5, the delay recurrence, L3904:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3904

Two properties of that recurrence are worth stating because they are what the tests can actually observe without depending on a random draw. The next delay is drawn from a range whose upper bound is derived from the *previous* delay, so the sequence is not a fixed schedule and two clients that fail at the same moment do not retry in lockstep — that is the point of the jitter. And when the range has size one the draw is determined, which is the only condition under which a single delay is predictable enough to assert exactly.

## Where a client goes when it knows nothing

A client with no consensus cannot ask the network for directory information, because choosing a cache from the network requires the consensus it does not have. The bootstrap escape is a list of directory mirrors compiled into the release.

**dir-spec.txt §5.1 (Downloading network-status documents), fallback list, L3691:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3691

The ordering between the two kinds of starting point is normative and is the reason a fallback list is not merely a convenience: clients prefer fallbacks to the authorities, trying them earlier and more often. Sending every cold-start client to a handful of authorities would concentrate load on precisely the hosts whose availability the whole network depends on.

**dir-spec.txt §5.1, preference for fallbacks over authorities, L3702:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3702

Once a consensus is in hand the client is in the ordinary regime, selecting caches and using directory information under the general client rules.

**dir-spec.txt §5 (Client operation), L3680:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3680

**dir-spec.txt §5.4 (Using directory information), L3842:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3842

Numeric behaviour that the network may need to change without a client release arrives as consensus parameters rather than as constants.

**param-spec.txt §1 (Parameters in the consensus), L22:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L22

**dir-spec.txt, the consensus "params" line, L2085:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2085

Entry-guard selection continues to govern the first hop of every circuit built here, including circuits built to satisfy an isolated request.

**guard-spec.txt §4.2 (The guard sample), L312:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L312

## What this checkpoint decides for itself

The following are **not** constrained by torspec, and the tests grade this implementation's documented behaviour rather than conformance to a standard:

- The representation of an isolation token, the rule for when two tokens may share a circuit, and the API through which a caller supplies one. The spec supplies only the requirement that SOCKS credentials can separate streams (socks-extensions.txt §1.1, L39).
- The order and timing of connection attempts when a host resolves to both IPv4 and IPv6 addresses. The "happy eyeballs" approach is RFC 8305, which is not part of torspec and carries no Tor-specific requirements; it is a dialing policy this client adopts.
- The shape of the DNS-port configuration, and the fact that a DNS port exists at all. The spec establishes only that SOCKS cannot carry this traffic (socks-extensions.txt §1.1, L42).
- The concrete retry parameters — attempt counts, initial delays, and caps — as distinct from the backoff *algorithm*, which is specified (dir-spec.txt §5.5, L3898).
- The taxonomy of error types, the dormant-mode state machine, and the interfaces through which directory state is published to consumers. These are API design, graded against `TEST_INTERFACE.md`.
