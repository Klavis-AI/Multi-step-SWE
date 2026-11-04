# Spec notes — step 11

What step 11 is graded against, where that comes from, and — just as
importantly — which parts of it no specification governs at all.

Everything cited here is read from the pinned 2022 torspec corpus: torspec at
commit `1db24dddbf2203f2a465747302f133ef3ed6eed4`, dated 2022-11-28, the
revision contemporary with the arti release this checkpoint targets. Line
anchors are into that commit and nothing else. Upstream has moved since, and a
line number taken from today's torspec will not land where these do. Every
anchor is written as

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

The prose below is this package's own. The specifications are quoted by
reference, never by transcription — torspec carries no licence, so if you want
the exact words, follow the link.

Two warnings before you start.

First, a large part of step 11 has no specification behind it. Zeroizing
secret buffers, the configuration-directory loader, the `PaddingLevel`
configuration enum, the relay-identity *set* type, and the `Send` bound on a
runtime's incoming-connection stream are all arti's own engineering. Section 12
lists them and says what governs them instead. Do not go looking for a torspec
paragraph about `SecretBuf`; there is none, and inventing one will lead you to
the wrong design.

Second, where arti deliberately differs from a specification SHOULD, this
document says so and the tests grade arti's behaviour, not the specification's.
Section 8 is the one place in this step where that happens, and it is called
out there explicitly so nobody "fixes" it into failing.

## 1. The PADDING_NEGOTIATE cell

This is the one new wire object in step 11. Everything about its layout is
fixed by one short subsection of tor-spec.

**tor-spec.txt §7.2 ("Link padding"), L2024:**
[tor-spec.txt#L2024](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2024)

Padding negotiation exists only on link protocol version 5 and above, and it
is the client that initiates it.

**tor-spec.txt §7.2, the link-protocol-5 precondition, L2031:**
[tor-spec.txt#L2031](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2031)

**tor-spec.txt §7.2, clients negotiate with a CELL_PADDING_NEGOTIATE command, L2033:**
[tor-spec.txt#L2033](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2033)

The body is six bytes, in this order: a one-byte version, a one-byte command,
then two two-byte millisecond bounds. There are no other fields and no padding
inside the body.

**tor-spec.txt §7.2, the Version field, L2036:**
[tor-spec.txt#L2036](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2036)

**tor-spec.txt §7.2, the Command field, L2037:**
[tor-spec.txt#L2037](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2037)

**tor-spec.txt §7.2, the ito_low_ms field, L2038:**
[tor-spec.txt#L2038](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2038)

**tor-spec.txt §7.2, the ito_high_ms field, L2039:**
[tor-spec.txt#L2039](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2039)

Only version 0 is defined. A parser that accepts any other version byte is
wrong, and the `padnegcell` group tests exactly that rejection: a body whose
first byte is not zero must fail to parse rather than be interpreted
optimistically.

**tor-spec.txt §7.2, only version 0 is defined, L2041:**
[tor-spec.txt#L2041](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2041)

The command byte takes exactly two values, and they are not in the order a
reader guesses: stop is 1 and start is 2.

**tor-spec.txt §7.2, command 1 stops and command 2 starts, L2042:**
[tor-spec.txt#L2042](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2042)

That inversion is why `PaddingNegotiateCmd::STOP` is 1 and
`PaddingNegotiateCmd::START` is 2, and why the suite pins both numbers
separately instead of trusting declaration order.

The two bounds are unsigned milliseconds, which is where the 16-bit ceiling
comes from: a bound above 65535 has no encoding. Arti's internal padding
parameters are held as 32-bit millisecond quantities, so converting a
`Parameters` value into a cell is fallible, and the `padparams` group grades
the refusal rather than a silent truncation.

**tor-spec.txt §7.2, bounds are unsigned millisecond integers, L2044:**
[tor-spec.txt#L2044](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2044)

**tor-spec.txt §7.2, ito_low_ms relative to nf_ito_low, L2045:**
[tor-spec.txt#L2045](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2045)

**tor-spec.txt §7.2, ito_high_ms must not be below ito_low_ms, L2047:**
[tor-spec.txt#L2047](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2047)

**tor-spec.txt §7.2, the receiver clamps out-of-range values, L2048:**
[tor-spec.txt#L2048](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2048)

Note what the last one does *not* say. The receiver clamps; the sender is not
required to. Arti therefore encodes the bounds it was given, and the range
check it performs is the encodability check (does this fit in a `u16`?), not a
policy check against the consensus. A client that refused to send 100 ms
because the consensus says 1500 would be implementing the receiver's job.

## 2. Where the cell sits in the cell taxonomy

`PADDING_NEGOTIATE` is a fixed-length cell with command byte 12. The
`cellfmt` group inherits the step-09/10 cell machinery and re-grades the
command table because a new entry was added to it at this checkpoint.

**tor-spec.txt §3 ("Cell Packet format"), L488:**
[tor-spec.txt#L488](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L488)

**tor-spec.txt §3, the fixed-length command table, L525:**
[tor-spec.txt#L525](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L525)

**tor-spec.txt §3, PADDING is command 0, L528:**
[tor-spec.txt#L528](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L528)

**tor-spec.txt §3, PADDING_NEGOTIATE is command 12, L539:**
[tor-spec.txt#L539](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L539)

Twelve is the largest fixed-length command number, and it is immediately
followed in the table by the variable-length block, which starts at 7 and
resumes at 128. That discontinuity is the reason `ChanCmd::is_var_cell` cannot
be a simple comparison.

**tor-spec.txt §3, the variable-length command list, L541:**
[tor-spec.txt#L541](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L541)

**tor-spec.txt §3, VERSIONS is command 7, L543:**
[tor-spec.txt#L543](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L543)

**tor-spec.txt §3, VPADDING is command 128, L544:**
[tor-spec.txt#L544](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L544)

**tor-spec.txt §3, command 7 or at least 128 means variable-length, L513:**
[tor-spec.txt#L513](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L513)

The circuit-ID rules are unchanged from earlier steps but are re-graded here
because `ChanCmd::accepts_circid_val` is part of the same table.

**tor-spec.txt §3, CIRCID_LEN is 2 through link protocol 3 and 4 thereafter, L517:**
[tor-spec.txt#L517](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L517)

**tor-spec.txt §0.2 ("Security parameters"), PAYLOAD_LEN is 509, L135:**
[tor-spec.txt#L135](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L135)

**tor-spec.txt §0.2, CELL_LEN is 512 below link protocol 4 and 514 from it, L139:**
[tor-spec.txt#L139](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L139)

509 is `CELL_DATA_LEN`. A six-byte PADDING_NEGOTIATE body is padded out to it
by the channel layer, which is why the `padnegcell` and `padparams` cases
assert on the *body* produced by `write_body_onto` and not on a whole cell.

Every multi-byte integer in these bodies is big-endian, including the two
millisecond bounds. There is no per-field endianness note anywhere in §7.2
because the rule is stated once, globally.

**tor-spec.txt §0.1.1 ("Encoding integers"), L110:**
[tor-spec.txt#L110](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L110)

**tor-spec.txt §0.1.1, numeric values are network byte order, L112:**
[tor-spec.txt#L112](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L112)

That is the whole justification for the `bytesrw` group's insistence that
`take_u16`, `take_u32`, `write_u16` and `write_u32` are big-endian: get it
wrong and 1500 becomes 56325 on the wire.

## 3. Why connection-level padding exists

The `padnegcell`, `padparams`, `padinsns`, `padupdates`, `padlevel`,
`chanconfig` and `chanusage` groups all grade pieces of one defence. It is
worth knowing what the defence is, because several of the numbers below look
arbitrary until you do.

**padding-spec.txt §1 ("Overview"), L40:**
[padding-spec.txt#L40](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L40)

**padding-spec.txt §1, connection-level padding uses the CELL_PADDING command, L45:**
[padding-spec.txt#L45](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L45)

**padding-spec.txt §2.1 ("Background"), L70:**
[padding-spec.txt#L70](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L70)

The adversary is not a traffic analyst watching packets; it is a router
emitting flow records. Those records are produced by Netflow and its
relatives.

**padding-spec.txt §2.1, metadata retention via Netflow, jFlow, Netstream and IPFIX, L76:**
[padding-spec.txt#L76](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L76)

**padding-spec.txt §2.1, the inactive flow timeout, L105:**
[padding-spec.txt#L105](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L105)

**padding-spec.txt §2.1, the inactive timeout ranges from 10 to 600 seconds, L109:**
[padding-spec.txt#L109](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L109)

**padding-spec.txt §2.1, a minimal amount of padding collapses split records, L138:**
[padding-spec.txt#L138](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L138)

So the goal is not to hide bytes. It is to keep a connection from ever looking
idle for as long as the cheapest router's inactive timer, so that many short
records collapse into one long one. Ten seconds is the floor observed across
real hardware, and every default in the next section is chosen to stay
comfortably under it.

## 4. The padding timer and its default bounds

**padding-spec.txt §2.2 ("Implementation"), L152:**
[padding-spec.txt#L152](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L152)

**padding-spec.txt §2.2, a timer per application TLS connection, L162:**
[padding-spec.txt#L162](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L162)

**padding-spec.txt §2.2, the default range is 1.5 to 9.5 seconds, L165:**
[padding-spec.txt#L165](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L165)

1500 and 9500 milliseconds. These are the two numbers that
`Parameters::default_padding()` returns, that the default `PaddingNegotiate`
start cell carries, and that `NetParameters` defaults `nf_ito_low` and
`nf_ito_high` to. The `padparams`, `dirparams` and `padnegcell` groups each
assert them independently, and they are meant to agree.

**padding-spec.txt §2.2, the timer resets whenever a cell is sent, L172:**
[padding-spec.txt#L172](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L172)

**padding-spec.txt §2.2, one CELL_PADDING cell on expiry, L175:**
[padding-spec.txt#L175](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L175)

**padding-spec.txt §2.2, always transmit before the minimum 10 second inactive timeout, L181:**
[padding-spec.txt#L181](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L181)

That last line is the design constraint the whole scheme hangs on, and it
explains 9500: the high bound must leave headroom under 10000.

The timeout is not sampled uniformly.

**padding-spec.txt §2.3 ("Padding Cell Timeout Distribution Statistics"), L193:**
[padding-spec.txt#L193](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L193)

**padding-spec.txt §2.3, sample from max of two uniform draws, L195:**
[padding-spec.txt#L195](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L195)

**padding-spec.txt §2.3, the resulting probability mass function, L199:**
[padding-spec.txt#L199](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L199)

Nothing in step 11 implements this distribution. The step delivers the
*parameters* — a low and a high bound, carried in a cell and in a
configuration object — and the timer that consumes them lives elsewhere. The
suite therefore does not sample anything, which is also what keeps it free of
unseeded randomness.

## 5. Reducing and disabling padding

`PaddingLevel` has exactly three values because negotiation has exactly three
outcomes.

**padding-spec.txt §2.5 ("Reducing or Disabling Padding via Negotiation"), L239:**
[padding-spec.txt#L239](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L239)

**padding-spec.txt §2.5, mobile clients may disable or reduce padding, L241:**
[padding-spec.txt#L241](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L241)

**padding-spec.txt §2.5, reduced padding samples from 9000 to 14000 milliseconds, L246:**
[padding-spec.txt#L246](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L246)

**padding-spec.txt §2.5, disabling sends a negotiate cell and then stops padding, L261:**
[padding-spec.txt#L261](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L261)

Reduced padding is deliberately *worse* than no padding at hiding activity
from a 10-second-timeout router — 9000 to 14000 straddles it — and that is the
accepted trade for battery life. The ordering `None < Reduced < Normal` that
the `padlevel` group grades is therefore an ordering by padding volume, not by
safety, and it is fixed by the order the variants are declared in.

**padding-spec.txt §2.5, negotiation happens at channel setup after NETINFO, L265:**
[padding-spec.txt#L265](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L265)

The single most consequential sentence for `padparams` is the next one.

**padding-spec.txt §2.5, re-enabling default padding sends START with zero bounds, L271:**
[padding-spec.txt#L271](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L271)

**padding-spec.txt §2.5, the recipient's clamping restores its own consensus values, L275:**
[padding-spec.txt#L275](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L275)

Zero bounds mean "use your own consensus", not "pad every zero milliseconds".
A client returning to normal padding must not copy 1500/9500 out of its own
consensus into the cell, because the two endpoints may hold different
consensuses. That is why `PaddingNegotiate::start_default()` exists as a
separate constructor from `start(low, high)`, why its encoded body is
`00 02 00 00 00 00`, and why it is also what `PaddingNegotiate::default()`
returns.

**padding-spec.txt §2.5, clients and bridges must reject negotiation from relays, L279:**
[padding-spec.txt#L279](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L279)

Step 11 grades the cell type, not the channel state machine, so this rule is
not exercised by any case. It is recorded here because a reader of
`PaddingNegotiate` will reasonably wonder why a client-only client type is
also `Readable`: the answer is that it must be parseable in order to be
recognised and refused.

## 6. The consensus parameters that govern padding

These are the names `NetParameters` must accept from a consensus, and the
values it must fall back to when a consensus does not mention them.

**padding-spec.txt §2.6 ("Consensus Parameters Governing Behavior"), L282:**
[padding-spec.txt#L282](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L282)

**padding-spec.txt §2.6 — the "nf_ito_low" line, L286:**
[padding-spec.txt#L286](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L286)

**padding-spec.txt §2.6, nf_ito_low defaults to 1500, L288:**
[padding-spec.txt#L288](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L288)

**padding-spec.txt §2.6 — the "nf_ito_high" line, L290:**
[padding-spec.txt#L290](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L290)

**padding-spec.txt §2.6, nf_ito_high defaults to 9500, L292:**
[padding-spec.txt#L292](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L292)

**padding-spec.txt §2.6, both bounds at zero disables padding, L293:**
[padding-spec.txt#L293](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L293)

**padding-spec.txt §2.6 — the "nf_ito_low_reduced" line, L295:**
[padding-spec.txt#L295](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L295)

**padding-spec.txt §2.6, nf_ito_low_reduced defaults to 9000, L298:**
[padding-spec.txt#L298](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L298)

**padding-spec.txt §2.6 — the "nf_ito_high_reduced" line, L300:**
[padding-spec.txt#L300](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L300)

**padding-spec.txt §2.6, nf_ito_high_reduced defaults to 14000, L303:**
[padding-spec.txt#L303](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/padding-spec.txt#L303)

That is the complete set of four that the `dirparams` group pins. The other
parameters in the same list — the connection timeouts, the relay-padding and
pad-before-usage switches — govern behaviour no step-11 type holds, and are
left ungraded.

param-spec repeats the names in its own index, though without values; the
authoritative defaults are the ones above.

**param-spec.txt §10 ("Padding-related parameters"), L340:**
[param-spec.txt#L340](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L340)

**param-spec.txt §10 — the "nf_ito_low" line, L362:**
[param-spec.txt#L362](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L362)

## 7. How parameters arrive, and why clamping is mandatory

A consensus parameter is an untrusted signed integer with a name. That is all
the transport guarantees.

**dir-spec.txt §3.4.1 ("Vote and consensus status document formats") — the "params" line, L2085:**
[dir-spec.txt#L2085](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2085)

**dir-spec.txt §3.4.1, a parameter is a keyword, an equals sign and an Int32, L2089:**
[dir-spec.txt#L2089](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2089)

**dir-spec.txt §3.4.1, Int32 spans the full signed 32-bit range, L2090:**
[dir-spec.txt#L2090](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2090)

**dir-spec.txt §3.4.1, keys are case-sensitive and sorted lexically, L2093:**
[dir-spec.txt#L2093](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2093)

**dir-spec.txt §3.4.1, meanings live in param-spec, L2100:**
[dir-spec.txt#L2100](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2100)

Two consequences drive the `dirparams` and `ccparams` groups.

First, an authority — or an attacker who has compromised enough of them — can
vote `nf_ito_high=2000000000`, and it is syntactically valid. A client must
clamp, not trust. Arti's ceiling for the padding timers is 60000 milliseconds
(`CHANNEL_PADDING_TIMEOUT_UPPER_BOUND`), which is arti's own choice: the
specification states no upper bound on these four parameters. Clamping is
saturating, not rejecting, because a single hostile parameter must not
invalidate an entire consensus.

Second, the parameter list is open-ended. A name a client does not recognise
must be ignored, silently and individually, so that a consensus carrying a
parameter from a newer Tor does not break an older client. `NetParameters`
returns the count of names it *did* recognise, and the unrecognised remainder
is not an error.

param-spec is where the ranges the `ccparams` and `dirparams` groups assert
come from, for those parameters that predate congestion control.

**param-spec.txt §1 ("Network protocol parameters") — the "circwindow" line, L24:**
[param-spec.txt#L24](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L24)

**param-spec.txt §1, circwindow is bounded 100 to 1000 with default 1000, L28:**
[param-spec.txt#L28](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L28)

**param-spec.txt §1 — the "ExtendByEd25519ID" line, L53:**
[param-spec.txt#L53](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L53)

**param-spec.txt §1, ExtendByEd25519ID is a 0/1 flag defaulting to 0, L55:**
[param-spec.txt#L55](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L55)

**param-spec.txt §1 — the "sendme_emit_min_version" line, L58:**
[param-spec.txt#L58](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L58)

**param-spec.txt §1, SENDME versions are bounded 0 to 255 with default 0, L59:**
[param-spec.txt#L59](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L59)

**param-spec.txt §1 — the "sendme_accept_min_version" line, L62:**
[param-spec.txt#L62](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/param-spec.txt#L62)

The 0-to-255 range is why the `ccparams` group tests that a voted SENDME
version of 300 saturates to 255 and of -1 to 0: a SENDME version is one byte
on the wire, so a wider consensus value has nowhere to go.

### 7a. The congestion-control parameters, and why they are not line-anchored

The five names the `ccparams` group maps — `cc_bwe_min`, `cc_ewma_cwnd_pct`,
`cc_ewma_max`, `cc_sendme_inc` and `cc_cwnd_inc_rate` — are not in
param-spec.txt at this commit. They come from proposal 324 (RTT-based
congestion control), which in this corpus lives under `proposals/` and had not
been merged into the numbered specifications by 2022-11-28.

This document deliberately gives no `#L` anchor for them. A proposal file is
not reachable by the same file-name convention as the specifications, the
proposal is a design document rather than normative text, and an anchor into
it would read as more settled than it is. What the `ccparams` group actually
grades is arti's mapping from those consensus names to its own field names and
the ranges it clamps them into, all of which are arti's engineering decisions
at this checkpoint:

| Consensus name | `NetParameters` field | Range | Default |
| --- | --- | --- | --- |
| `cc_bwe_min` | `cc_min_sendme_acks` | 2 to 20 | 5 |
| `cc_ewma_cwnd_pct` | `cc_ewma_n_by_sendme_acks` | 1 to 255 percent | 50 |
| `cc_ewma_max` | `cc_ewma_n_max` | 2 upward | 10 |
| `cc_sendme_inc` | `cc_sendme_cell_ack_count` | 1 to 255 | 31 |
| `cc_cwnd_inc_rate` | `cc_cwnd_inc_rate` | 1 to 250 | 1 |

The renaming is the point. `cc_ewma_cwnd_pct` is a percentage and
`cc_ewma_n_by_sendme_acks` is typed as one, so a consensus value of 50 means
50 percent and not 0.5 — the `ccparams` group has a case for exactly that
confusion, because it is the mistake a reader of the proposal makes.

The one flow-control fact that *is* normative at this commit is the
subprotocol number that advertises congestion control; see section 9.

## 8. Relay identities

Step 11 generalises "a relay identity" from a fixed pair into a set of
possibly-absent, possibly-unknown identities. The two identity types, and the
rule that binds them, are old.

**tor-spec.txt §1.1 ("Keys and names"), L225:**
[tor-spec.txt#L225](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L225)

**tor-spec.txt §1.1, the RSA and Ed25519 identities together identify a router, L263:**
[tor-spec.txt#L263](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L263)

Together — and permanently. Once a pair has been used, neither key may ever
appear with a different partner. That is why `RelayIdSet` keeps one namespace
per identity type rather than a single flat set, why `same_relay_ids` compares
every type rather than any type, and why `has_all_relay_ids_from` is
deliberately asymmetric: a target that knows more identities than a query
satisfies the query, but not the reverse.

Both identities appear on the wire as link specifiers in EXTEND2.

**tor-spec.txt §5.1.2 ("EXTEND and EXTENDED cells"), L1082:**
[tor-spec.txt#L1082](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1082)

**tor-spec.txt §5.1.2, type 00 is an IPv4 ORPort, L1101:**
[tor-spec.txt#L1101](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1101)

**tor-spec.txt §5.1.2, type 01 is an IPv6 ORPort, L1103:**
[tor-spec.txt#L1103](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1103)

**tor-spec.txt §5.1.2, type 02 is the 20-byte legacy identity, L1105:**
[tor-spec.txt#L1105](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1105)

**tor-spec.txt §5.1.2, type 03 is the 32-byte Ed25519 identity, L1107:**
[tor-spec.txt#L1107](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1107)

20 and 32 are the byte lengths `RelayIdType::id_len()` returns, and they are
why an Ed25519 identity is 43 unpadded base64 characters and a legacy identity
is 40 hexadecimal ones.

**tor-spec.txt §5.1.2, the recommended link-specifier order, L1114:**
[tor-spec.txt#L1114](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1114)

**This is the step's one deliberate divergence, and the `owntarget` group
grades arti's behaviour rather than the specification's.** The specification
recommends emitting link specifiers in the order 00, 02, 03, 01 — IPv4,
legacy, Ed25519, IPv6 — for indistinguishability from C tor. Arti at this
checkpoint emits identities first, in identity-type order (Ed25519 then RSA),
and then addresses in the order the target lists them. The
`owntarget__linkspecs_order_ids_before_addrs` case asserts the arti order.
Producing the specification's order would fail it. If you are reading this
because that case failed, the fix is to match arti, not torspec; the
divergence is upstream's and is out of scope for this step.

The textual forms that `RelayId`'s parser accepts are also not invented here.
The dollar-prefixed 40-hex form is the control protocol's fingerprint syntax.

**control-spec.txt §2.4 ("General-use tokens"), a fingerprint is a dollar sign and 40 hex digits, L303:**
[control-spec.txt#L303](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/control-spec.txt#L303)

**dir-spec.txt §2.1.1 ("Server descriptor format") — the "fingerprint" line, L565:**
[dir-spec.txt#L565](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L565)

**dir-spec.txt §2.1.1, a fingerprint is hex with a space every four characters, L569:**
[dir-spec.txt#L569](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L569)

Note that arti's parser accepts the un-spaced 40-character form, with or
without the leading dollar sign, and canonicalises to the dollar-prefixed
lowercase form. It does not accept the space-separated descriptor rendering.
The `relayid` group grades that: `$` plus hex and bare hex both parse, case is
normalised, and `Display` always re-emits the dollar form.

The base64 form of an Ed25519 identity comes from the directory documents.

**dir-spec.txt §3.3 ("Computing microdescriptors") — the "id" line for rsa1024, L1764:**
[dir-spec.txt#L1764](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1764)

**dir-spec.txt §3.3, base64 encoded without trailing equals signs, L1768:**
[dir-spec.txt#L1768](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1768)

**dir-spec.txt §3.3 — the "id" line for ed25519, L1778:**
[dir-spec.txt#L1778](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1778)

**dir-spec.txt §3.3, the master Ed25519 identity, base64, no trailing equals, L1782:**
[dir-spec.txt#L1782](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1782)

**dir-spec.txt §3.4.1 — the "r" line of a router status entry, L2291:**
[dir-spec.txt#L2291](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2291)

**dir-spec.txt §3.4.1, the identity is base64 with trailing equals removed, L2296:**
[dir-spec.txt#L2296](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L2296)

"Without trailing equals signs" is the reason 32 bytes render as exactly 43
characters, and the reason `RelayId`'s parser uses that length as the
discriminator when no algorithm prefix is present. The `ed25519:` prefix
itself is arti's own syntax; no specification defines a prefixed textual form.

## 9. Subprotocol versions

The `protover` group is inherited, but two of its numbered kinds exist
because of features this step touches.

**tor-spec.txt §9 ("Subprotocol versioning"), L2186:**
[tor-spec.txt#L2186](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2186)

**tor-spec.txt §9.1, the Link versions run 1 through 5, L2249:**
[tor-spec.txt#L2249](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2249)

**tor-spec.txt §9.11 ("Padding"), L2418:**
[tor-spec.txt#L2418](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2418)

**tor-spec.txt §9.12 ("FlowCtrl"), L2430:**
[tor-spec.txt#L2430](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2430)

The "Padding" subprotocol is about *circuit*-level padding machines, not the
connection-level padding this step implements — connection padding is gated on
link protocol 5 instead. The `protover` group pins `ProtoKind::Padding` at 10
and `ProtoKind::FlowCtrl` at 11 as part of a numbering that must not drift;
the numbering itself is arti's enumeration of the names in section 9, in the
order they appear.

The wire syntax of a protocol list is a directory-document grammar.

**dir-spec.txt §2.1.1 — the "proto" line, L926:**
[dir-spec.txt#L926](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L926)

**dir-spec.txt §2.1.1, entries sorted by keyword, values ascending, ranges compact, and no version above 63, L950:**
[dir-spec.txt#L950](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L950)

**dir-spec.txt §3.3 — the "pr" line, L1799:**
[dir-spec.txt#L1799](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1799)

The 63 ceiling is why `Protocols` stores recognised versions as a bitmask and
why a version above it is an out-of-range parse error rather than a
`u64` that happens not to fit. Unrecognised protocol *names* are a different
matter: those are kept verbatim, because a client must be able to re-emit a
list it does not fully understand.

## 10. VERSIONS and NETINFO

Both are inherited by `cellfmt`, and both matter here for a reason.

**tor-spec.txt §4.1 ("Negotiating versions with VERSIONS cells"), L638:**
[tor-spec.txt#L638](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L638)

**tor-spec.txt §4.1, the payload is a series of big-endian two-byte integers, L659:**
[tor-spec.txt#L659](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L659)

**tor-spec.txt §4.1, both parties select the highest version in common, L660:**
[tor-spec.txt#L660](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L660)

**tor-spec.txt §4.1, an odd number of body bytes is malformed, L664:**
[tor-spec.txt#L664](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L664)

**tor-spec.txt §4.1, link protocol 5 adds link padding and negotiation, L685:**
[tor-spec.txt#L685](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L685)

Link protocol 5 is the gate for everything in sections 1 and 4 of this
document, which is why a step about padding re-grades version negotiation.
`Versions::encode_for_handshake` frames the cell with a two-byte circuit ID
regardless of the version being negotiated, because the first VERSIONS cell
precedes agreement on circuit-ID width.

**tor-spec.txt §4.5 ("NETINFO cells"), L939:**
[tor-spec.txt#L939](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L939)

**tor-spec.txt §4.5, the payload begins with a four-byte timestamp, L944:**
[tor-spec.txt#L944](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L944)

**tor-spec.txt §4.5, the timestamp is big-endian seconds since the Unix epoch, L964:**
[tor-spec.txt#L964](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L964)

**tor-spec.txt §4.5, clients should send zero as their timestamp, L966:**
[tor-spec.txt#L966](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L966)

`Netinfo::for_client` must emit a zero timestamp. This is not a convenience:
a real clock value fingerprints the client, and it would also make the
`cellfmt` group nondeterministic, which is why the case asserts the four
leading bytes are zero rather than asserting the cell merely encodes.

Padding negotiation is sent immediately after NETINFO (section 5 above), so
these two cells are adjacent in the handshake this step extends.

## 11. Directory parameters before the directory is complete

The `dirparams` group grades a provider interface, not a document format, and
that interface has no specification. What does have one is the reason it
exists: a client needs parameter values before it holds a usable consensus,
and the consensus it eventually gets may be unusable for reasons that have
nothing to do with parameters.

The `Timeliness` distinction the group tests — a strictly-valid directory, a
merely-timely one, and one whose validity interval is not checked at all — is
arti's model of that problem. The relevant normative facts are the ones in
section 7: a parameter is a name and a signed integer, missing names fall back
to defaults, and unknown names are ignored. A provider that hands out
compiled-in defaults before any consensus arrives is consistent with all
three.

`ids_listed` is tri-state for the same reason. "This relay is not in the
directory" and "I cannot tell you, because the directory I hold does not cover
this relay's identities" are different answers, and collapsing them into a
boolean is how a client ends up treating an unknown relay as a rejected one.

## 12. What has no specification at all

These parts of step 11 are graded, and nothing in torspec governs them. Do not
go looking.

**`SecretBuf` and zeroization.** The specifications say nothing about how an
implementation holds key-derivation input in memory. `SecretBuf`'s
384-byte initial capacity, its doubling growth rule, and its habit of zeroing
the old allocation *before* releasing it are arti's engineering. The one
externally-visible consequence the `secretbuf` group grades is that growth
reallocates one byte early — at capacity rather than past it — because the
copy-and-zeroize path must run before the underlying `Vec` would relocate on
its own.

**`PaddingLevel` as a configuration value.** Three levels, snake-case text,
declaration-order comparison, and a legacy boolean accepted on deserialization
where `true` means normal and `false` means none. The three *outcomes* are
from padding-spec section 5 above; the enum, its text forms and its
backwards-compatible boolean are arti's configuration design.

**`ConfigurationSource` and `ConfigurationSources`.** The configuration
directory, the file-versus-directory distinction, sorted merge order, the
must-read versus tolerate-absence flag, and the filesystem-permission check
are an application concern. Tor's own torrc format is not specified in torspec
either.

**`tor_config::load::resolve` and the deprecated-key report.** Multiple
consumers resolving from one tree, unrecognised keys reported by intersection
rather than treated as fatal, and deprecated keys enumerated as warnings: all
arti.

**`ErrorKind` and `Report`.** Inherited from step 10 and still unspecified.
The `Display` strings the `errkind` group pins are arti's API surface, not
protocol text.

**The `Send` bound on `TcpListener::Incoming`.** This is a Rust type-system
change with no protocol meaning whatsoever. It is graded because it is the
only change in `tor-rtcompat`'s trait module between the previous checkpoint
and this one, and because a runtime implementation that omits it stops
compiling in every caller that boxes the stream — a failure that surfaces far
from its cause.

**`IoErrorExt::is_not_a_directory` and `anonymize_home`.** Platform
plumbing and log hygiene respectively.

## 13. Reading order

If you are implementing this step and want the specifications in the order the
code needs them: tor-spec §7.2 first, for the cell; then padding-spec §2.2 and
§2.6, for the numbers the cell carries; then padding-spec §2.5, for why a
start cell with zero bounds is not a bug; then dir-spec §3.4.1 and param-spec
§1, for how parameters arrive and why every one of them is clamped; then
tor-spec §1.1 and §5.1.2, for the two identity types and their wire forms.
Everything else in this document is either inherited from an earlier step or
listed in section 12 as arti's own.
