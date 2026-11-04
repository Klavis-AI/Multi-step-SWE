# Specification notes — step 04

First-party prose stating the normative protocol and design facts this step is graded against. Every fact is cited to a numbered section of the Tor specifications at commit `1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28) with a line-anchored URL. No specification body text is reproduced; the citations are to section headings and the prose below is written for this workspace.

One caution before the citations: a good part of this checkpoint is *not* specified by torspec at all. Predictive circuit construction, the shape of a configuration object, and the rules for changing settings on a running client are implementation policy. Where that is so, this document says so explicitly rather than reaching for an anchor that does not support the claim. A citation here means the spec constrains the behaviour; its absence means the client is free, and the test grades this implementation's documented choice instead.

## Building circuits before they are asked for

A client that waited for a request before building a circuit would make the user wait through a full circuit construction on every first connection. Clients therefore build a small number of circuits in advance, predicting the kinds of connection the user is likely to make from the kinds they have recently made. A predicted circuit is an ordinary circuit; what differs is only the reason it was built and the fact that nobody is waiting on it yet.

**path-spec.txt §2.1.1 (Clients build circuits preemptively), L207:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L207

Circuits are also built on demand, when a request arrives and no existing circuit can carry it. The two paths coexist: prediction reduces how often the on-demand path is reached, and never replaces it.

**path-spec.txt §2.1.2 (Clients build circuits on demand), L240:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L240

Circuits are not kept forever. A client tears down circuits that are too old or that no longer have a use, which is what makes a predicted circuit a bounded expense rather than an accumulating one.

**path-spec.txt §2.1.6 (When to tear down circuits), L290:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L290

A circuit is only useful for a request if its exit can carry that request. Prediction is therefore expressed in terms of what a future request would need — chiefly the port it would exit to — and not in terms of a count of circuits.

**path-spec.txt §2.2.1 (Choosing an exit), L383:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L383

**path-spec.txt §2.2 (Path selection and constraints), L303:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L303

## Which relays may appear together on one path

Relays under common control are declared as a family. A client must not treat two relays in the same family as independent, because they are not: using two of them on one circuit concentrates the path in one operator's hands. Family membership is declared by the relay itself and carried in its descriptor, and mutual declaration is what makes a family claim credible — an unreciprocated claim is not binding.

**dir-spec.txt §2.1.1 (Server descriptor format) — the "family" line, L801:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L801

**dir-spec.txt §3.3 (Computing microdescriptors) — the "family" line as it survives into a microdescriptor, L1709:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1709

**path-spec.txt §2.2 (Path selection and constraints), L303:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L303

Subnet diversity is the same principle applied to network position rather than declared ownership.

**dir-spec.txt §5.4.1 (Choosing routers for circuits), L3851:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3851

## Guards, restrictions, and when a guard set is sufficient

A client picks its entry points from a sampled, filtered subset of the consensus rather than uniformly at random, and keeps using them. Some circuits impose an additional restriction on which guard may be used — for instance, a guard that would collide with another relay already chosen for the path. A restriction narrows the usable set, so a client must be able to answer whether what remains is still enough to build with.

**guard-spec.txt §4.2 (The Usable Sample), L312:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L312

**guard-spec.txt §4.6 (Selecting guards for circuits), L466:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L466

**guard-spec.txt §4.4 (The Primary guards), L414:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L414

## Parameters that come from the consensus

Numeric tuning values are not compiled into a client. The directory authorities vote on them and publish the result on the consensus "params" line, so the network can be retuned without shipping new clients. A client reads the value from the consensus when it is present and falls back to its built-in default when it is not; values are range-checked, because the consensus is not a trusted source of arbitrary integers.

**dir-spec.txt §3.4.1 (Vote and consensus status document formats) — the "params" line, L2085:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2085

**param-spec.txt §1 (Network protocol parameters), L22:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L22

**param-spec.txt §2 (Performance-tuning parameters), L73:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L73

**param-spec.txt §4 (Circuit-build-timeout parameters), L140:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L140

How long to wait before giving up on a circuit is itself learned from observation and governed by consensus parameters, rather than being a fixed constant.

**path-spec.txt §2.4 (Learning when to give up ("timeout") on circuit construction), L436:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L436

**path-spec.txt §2.4.6 (Consensus parameters governing behavior), L607:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L607

## Tearing down a circuit, and saying why

A circuit is destroyed by a DESTROY cell, which carries a single-octet reason. The reason is advisory and must not be forwarded verbatim by a relay that passes the teardown along, because the reason a circuit failed at one hop may leak information about another. A client that receives a reason it does not recognise must not treat that as a protocol violation: the reason space is extensible.

**tor-spec.txt §5.4 (Tearing down circuits), L1474:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1474

## Streams on a circuit

Many streams share one circuit. Each is identified by a stream ID carried in the relay cell header, and the ID is meaningful only within its circuit. Zero is reserved: it marks a relay cell that concerns the circuit as a whole rather than any one stream, so a stream may never be assigned it.

**tor-spec.txt §6.1 (Relay cells), L1647:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1647

**tor-spec.txt §6.2 (Opening streams and transferring data), L1756:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1756

**tor-spec.txt §3 (Cell Packet format), L488:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L488

## Flow control and the two SENDME versions

Flow control exists at two levels — the circuit and the stream — and each is driven by SENDME cells that authorise more data. The circuit-level SENDME exists in two versions. The older form carries no payload. The newer form is authenticated: it carries a digest of the traffic it acknowledges, so that a relay cannot speed a client up by sending SENDMEs for data it never delivered. Which version is in use is negotiated by consensus parameter rather than assumed, and the cell format differs between them.

**tor-spec.txt §7.3 (Circuit-level flow control), L2053:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2053

**tor-spec.txt §7.3.1 (SENDME Cell Format), L2103:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2103

**tor-spec.txt §7.4 (Stream-level flow control), L2143:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2143

## Transport, and why the TLS layer is pluggable here

The link to a relay is a TLS connection, and the protocol does not fix which library performs the handshake. That is what makes it legitimate for this checkpoint to carry more than one TLS backend, and to exercise the client's networking over a mock transport in tests: the Tor protocol begins above that layer.

**tor-spec.txt §2 (Connections), L268:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L268

**tor-spec.txt §2.1 (Picking TLS ciphersuites), L422:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L422

## Directory information and the events that announce it

A client's view of the network changes as it fetches directory material, and code above the directory manager needs to learn when that view has changed rather than polling it.

**dir-spec.txt §5 (Client operation), L3680:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3680

**dir-spec.txt §5.2 (Downloading server descriptors or microdescriptors), L3760:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3760

**dir-spec.txt §5.4 (Using directory information), L3842:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3842

## What this checkpoint decides for itself

The following are graded, and are **not** derived from any specification. They are this implementation's own documented behaviour, and the tests state them as such:

- The existence and shape of a configuration object, its builder, and which of its fields have defaults.
- That some settings may be changed on a running client and others may not, and what happens when a caller tries to change one that may not be. A specification has nothing to say about a client's configuration API; the rule that a change either applies wholly or is rejected wholly is a design choice made here, and it is graded because callers depend on it.
- The particular predictive policy: how many circuits are kept ahead, how long a predicted port stays predicted, and what the defaults are. The spec establishes *that* clients predict; the numbers are this client's.
- The internal test scaffolding (mock transports, in-memory networks) used to make the above observable without a live network. Per scope decision S1, nothing in this step contacts the real Tor network at grading time.
