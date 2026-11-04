# Specification notes — step 12 (`12-bridges-and-pluggable-transports`)

First-party notes on the normative facts this step is graded against. Every
statement below is my own summary of a numbered section of the Tor
specifications; no specification text is reproduced. Read the cited section if
you need the full wording.

All citations are against torspec at the pinned commit
`1db24dddbf2203f2a465747302f133ef3ed6eed4`. The URL pattern is

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

`TEST_INTERFACE.md` says *what* the graded items are. This file says *why* they
have the shape they do. Where the two overlap, `TEST_INTERFACE.md` is the
authority on names and signatures; this file is the authority on protocol
meaning. Where a specification permits something this step does not build,
section 17 says so explicitly, so that the permission is not mistaken for a
requirement.

---

## 1. What a bridge is

A bridge is an ordinary Tor relay that deliberately keeps itself out of the
public directory. It runs the same OR protocol on the same kind of ORPort, has
the same kinds of identity keys, and is reached by the same channel handshake;
the only difference is that a client cannot learn about it from the consensus,
because it is not in the consensus. The client must therefore be *told* about
it out of band — by a web page, by mail, by a friend — and that out-of-band
message is the bridge line (section 3).

Everything that follows is downstream of that single fact. Because a bridge is
not in the consensus:

* the client has no consensus entry stating the bridge's addresses, identity
  keys, onion key or protocol versions, so it must fetch and parse the bridge's
  own **router descriptor** (section 6) instead of the microdescriptor it would
  use for a public relay;
* the client has no directory-authority signature vouching for that document,
  so it must verify the descriptor's *self*-signature and check that the keys
  inside it are the keys the bridge line claimed (section 6, section 7);
* the client's set of possible first hops is the configured bridge set rather
  than the consensus guard set (section 2); and
* the bridge's address is a secret worth protecting, which is why it is
  redacted in logs (section 16).

A bridge may additionally sit behind a **pluggable transport**: a separate
process that disguises the traffic on the wire so that a censor watching the
network cannot recognise it as Tor. Sections 8 to 14 cover that half.

## 2. Bridges in the guard algorithm

**guard-spec §2, "State instances"** —
[guard-spec.txt#L82](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L82)

When `UseBridges` is set, the set of candidate guards is replaced wholesale by
the list of configured bridges, and a separate persistent instance of the
sampled-guards and confirmed-guards state is kept for that case —
[guard-spec.txt#L92](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L92).
In the bridge case there is no upper limit on the sample size —
[guard-spec.txt#L98](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L98).

**guard-spec §4.2, "The Usable Sample"** —
[guard-spec.txt#L312](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L312)

The filter that derives the usable sample includes the condition that a
candidate is a bridge exactly when `UseBridges` is true, and is not a bridge
when it is false —
[guard-spec.txt#L332](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L332).
The two universes never mix: a client either builds circuits through bridges or
through consensus guards, never through an accidental blend of the two.

Separate state matters for a censorship-resistance reason and not only for
tidiness. If the bridge and non-bridge guard sets shared one persistent record,
a user who switched `UseBridges` off would carry bridge entries into the
non-bridge state file, and a user who switched it on would begin from guards
that a network observer had already seen them use.

The keeping of two state instances is not graded at step 12 through any public
item; what is graded is the model it implies — a bridge is described by a
`BridgeConfig`, is a `ChanTarget` in its own right, and reaches the channel
manager through the same `HasChanMethod` path a consensus relay does.

## 3. The bridge line

**bridgedb-spec §7, "Displaying Bridge Information"** —
[bridgedb-spec.txt#L357](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L357)

This is the section that fixes the shape of the text a user is handed. A plain
bridge is given out as an address and port —
[bridgedb-spec.txt#L363](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L363).
A bridge behind a pluggable transport is given out as the transport name, then
the address and port, then an optional argument list —
[bridgedb-spec.txt#L367](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L367)
— where that argument list is a space-separated sequence of `key=value` pairs —
[bridgedb-spec.txt#L369](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L369).
The same section records that lines used to be handed out with the word
`bridge` in front and that the practice was dropped —
[bridgedb-spec.txt#L372](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L372).

Torspec's proposal `125-bridges.txt` (§3.1, "Format of the bridge identifier")
gives the other half of the syntax as actually used in a `torrc`: the keyword
`bridge`, the address and port, and an identity fingerprint. The proposal file
is not part of the flat corpus, so it is named without a line anchor.

The grammar the client must accept is the union of those two, read as a
sequence of whitespace-separated words in this order:

1. optionally, the word `bridge`, in any letter case. It is there so that a
   line copied out of a `torrc` pastes cleanly. It carries no information: a
   line parsed with it and the same line parsed without it are the same bridge.
2. optionally, a pluggable-transport name (section 9). Absent, the client
   connects to the bridge itself over TLS, exactly as it would to a relay.
3. the `Host:ORPort` to connect to (section 5).
4. one or more identity fingerprints (section 4).
5. for a transport line only, zero or more `key=value` parameters, which are
   handed to the transport rather than interpreted by the client (section 14).

Two disambiguation rules make that grammar parseable in one left-to-right pass
without backtracking, and both are observable:

* **Transport or address?** A transport name is a C identifier (section 9) and
  therefore never contains a colon; an address always carries `:port`. So the
  word in position 2/3 is read as an address if it contains a colon and as a
  transport name otherwise. The consequence is that a malformed address with no
  colon — say a bare hostname — is read as a *transport name* and fails the
  transport-name rules, while a malformed address that does have a colon fails
  the address rules. Both diagnoses are correct; they simply name different
  things as the problem.
* **Once a transport has been named, the next word is its target.** The parser
  does not peek to see whether that word looks more like a fingerprint. A
  transport line whose next word is a fingerprint therefore fails on the
  *address*, and the failure is attributed to the transport whose target was
  being read, not to the identity.

Identity position then runs until the first word containing `=`, which begins
parameter position. That is why a second address-shaped word on a direct line is
reported as a bad identity rather than as a second address: the grammar gives a
direct line exactly one address word, and everything after it is an identity or
a parameter.

Further consequences the grammar forces, all of them graded:

* an empty or all-whitespace line has no words at all, and neither does a line
  that is only the keyword, so both are the same "nothing here" failure;
* leading and trailing whitespace on the line as a whole is insignificant, and
  runs of whitespace between words are one separator;
* parameters are meaningless without a transport to hand them to, so a direct
  line carrying `key=value` words is rejected rather than silently dropping
  them;
* `Display` must round-trip: printing a parsed bridge and re-parsing the result
  must give an equal value, because the printed form is what a client writes
  back into a configuration file or a log.

## 4. Identities on a bridge line

A bridge line's fingerprints are the entire basis for authenticating the
bridge. There is no consensus entry and no authority signature to fall back on,
so if the line's keys are wrong or absent the client has no way to tell a real
bridge from a machine that has taken over the address.

**dir-spec §2.1.1, "Server descriptor format"** —
[dir-spec.txt#L482](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L482)

The RSA identity is the one written on the `fingerprint` line of a descriptor:
a hex encoding of a hash of the ASN.1-encoded identity key, conventionally
grouped in fours, and a descriptor whose fingerprint line disagrees with its
key must be rejected —
[dir-spec.txt#L565](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L565).
That is the identity a bridge line is required to carry. The Ed25519 master
identity is the one carried by the `identity-ed25519` certificate —
[dir-spec.txt#L507](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L507)
— and restated on the `master-key-ed25519` line, which must match it —
[dir-spec.txt#L524](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L524).
A bridge line may name it in addition, and then both keys are checked.

The rules this step is graded on:

* an RSA identity is mandatory; a line that names no identity at all is
  rejected, and so is a line whose only identities are of other types;
* an Ed25519 identity is optional, and when present it is carried alongside
  rather than instead;
* at most one identity per key type — naming the same type twice is an error,
  because the two values cannot both be right and the client has no way to pick;
* a word in identity position that is not a well-formed identity of any known
  type, and does not contain `=`, is an error naming that word.

The relay-identity model itself (`RelayId`, `RelayIdRef`, `RelayIdType`,
`RelayIdSet`, `HasRelayIds`) was introduced at step 11 and is re-graded here;
its notes remain in `docs/step-11/`. What is new at step 12 is the *use*: a
bridge line is a partial identity record, a descriptor is a fuller one, and the
question "does this descriptor belong to the bridge I was told about?" is
exactly `has_all_relay_ids_from` in the direction descriptor-satisfies-line.
The asymmetry is the point. A line naming only an RSA key is satisfied by a
descriptor that also has an Ed25519 key; the reverse does not hold, and a line
naming a *different* Ed25519 key is satisfied by neither.

## 5. Addresses: numeric, named, and absent

A bridge address is written `Host:ORPort`. For a direct bridge, `Host` must be
a literal IP address — an IPv4 dotted quad, or an IPv6 address in square
brackets, which is also the form descriptors use for their alternative OR
addresses —
[dir-spec.txt#L893](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L893).
A direct connection has no way to resolve a name without leaking the lookup to
whichever resolver the censor controls, so a hostname is refused for a direct
bridge.

When a pluggable transport is in use, the target is nominal: the client never
opens a socket to it. It is handed to the transport process, which is free to
resolve it, ignore it, or use it as a rendezvous token. A hostname is therefore
acceptable there, and so is *no address at all* — some transports (a
domain-fronted or rendezvous-broker design, for instance) find their own way to
a bridge and need no address from the user.

Torspec defines no spelling for "no address" in a bridge line; arti writes it as
a single `-`, matching the way it writes the built-in transport (section 9).
There is no normative requirement behind the choice of character — but the
choice itself **is graded**: `-` is what the interface contract pins, and the
bridge-line and address cases assert it round-trips. Follow the convention.

The three cases must stay distinguishable in both directions:

* a numeric address stays a socket address and never reads back as a hostname;
* a named address stays a name and a port and never reads back as a socket
  address;
* the absent case is its own value, is not an error, and prints as `-`.

An address with no `:port` is rejected. There is no default ORPort to fall back
on: the whole point of a bridge is that it is somewhere unexpected.

## 6. Bridge descriptors

**dir-spec §2.1.1, "Server descriptor format"** —
[dir-spec.txt#L482](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L482)

A bridge publishes a full server descriptor (a "router descriptor"), not a
microdescriptor. As **dir-spec §3.3, "Microdescriptors"** records,
microdescriptors are a stripped-down form computed by the directory
authorities from the consensus —
[dir-spec.txt#L1649](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1649)
— and a bridge is not in the consensus, so no microdescriptor for it exists.
This is why router-descriptor parsing appears in a *client* at this step,
having been unnecessary for eleven steps.

The items that matter to a client deciding how to reach and extend through a
bridge:

* the `router` line opens the document and gives the nickname, an IPv4 address
  and the ORPort —
  [dir-spec.txt#L490](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L490).
  The SOCKSPort field is a historical artefact and is always zero.
* each `or-address` line adds a further address and port, and may be IPv6 —
  [dir-spec.txt#L893](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L893).
  The `router` line's address comes first and the `or-address` lines follow in
  document order; that ordering is the client's preference order.
* the `published` line is a UTC timestamp —
  [dir-spec.txt#L558](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L558).
* the `ntor-onion-key` line carries the curve25519 public key for the ntor
  handshake, base64-encoded, with the trailing `=` padding optionally removed —
  [dir-spec.txt#L633](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L633).
  Without it the client cannot extend a circuit through the bridge.
* the `proto` line states the subprotocol versions the relay supports, as
  `Keyword=Values` entries with comma-separated values and ranges —
  [dir-spec.txt#L926](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L926).
  It must print back in canonical form, because protocol sets are compared and
  logged.
* the `router-signature` line closes the document —
  [dir-spec.txt#L751](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L751)
  — and the signature covers the document from the start of the `router` line
  through the newline that ends the `router-signature` line —
  [dir-spec.txt#L758](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L758).

Two properties of that signature rule are graded and are worth stating plainly.
First, because the signed region ends at a known line, trailing blank lines
after `router-signature` are outside the signature and must be tolerated rather
than treated as corruption. Second, because the signed region *begins* at the
`router` line, altering any byte of the body — one digit of a port, one
character of a nickname — invalidates the signature. A parser that returns a
descriptor without ever checking the signature will pass every well-formed
input and fail exactly that case, which is why it is the case that makes the
group non-vacuous.

Descriptor parsing produces a value that is both signature-gated and
time-bounded. Those are two independent claims — "this document was signed by
the key it names" and "this document is currently in its validity window" — and
they are discharged separately. For a test fixture, only the signature claim is
meaningful: the archived descriptor's validity window is long past, so the
time bound is deliberately assumed rather than checked, which keeps the graded
behaviour free of any dependence on the wall clock.

## 7. Fetching and filing a bridge descriptor

A client that has only a bridge line knows an address and some keys, and
nothing else. Proposal `125-bridges.txt` (§3.4, "How bridge users get their
bridge descriptor") describes the two routes: ask the bridge directly for its
own descriptor, or ask the bridge authority for it by fingerprint, with the
direct query as the default. The direct query uses the general-use HTTP
resource for "the most recent descriptor for this server", listed in dir-spec's
Appendix B ("General-use HTTP URLs") as `/tor/server/authority`.

The direct route is the one that matters for anti-censorship: it needs no third
party, and it proves reachability and descriptor fetch in a single step. The
client opens a channel to the bridge using the bridge line's address and keys,
tunnels a directory request over it, and reads back the descriptor — which it
then checks against the keys the line claimed (section 4). That download itself
is **not** built at this step: section 17 records it, together with descriptor
caching and expiry, as out of scope, and what is graded here is the filing and
checking half described in the rest of this section.

**dir-spec §5.5, "Retrying failed downloads"** —
[dir-spec.txt#L3890](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890)

Bridge descriptors have their own base delay in the randomized-backoff schedule
that step 08 introduced: a large one (10800 seconds) once at least one bridge
is usable, and zero otherwise —
[dir-spec.txt#L3933](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3933).
The asymmetry is deliberate: while bootstrapping there is nothing to lose by
retrying immediately, and once working there is no reason to hammer a bridge
whose operator is probably running it from a home connection.

The storage model follows from all of the above. A downloaded descriptor is
filed under **the bridge line it was fetched for**, not under a fingerprint:
the line is what the user configured, it is what identifies the fetch, and two
lines naming the same relay at different ports are two different bridges as far
as reachability is concerned. A slot in that map may hold a *failure* instead of
a descriptor, and the failure has to carry a kind and a retry time, because
"this bridge is unreachable right now" and "this bridge line is nonsense" call
for very different client behaviour. The failure side is a boxed trait object
rather than one concrete type so that the reason can come from anywhere in the
stack — a channel error, a directory error, a transport error — without the
guard manager having to know about all of them.

The provider that owns that map hands it out by shared reference and announces
changes on a stream. It says only that *something* changed, not what; the
consumer re-reads the map. That is the cheapest correct design for a value that
several subsystems watch, and it avoids inventing a change-description type
nobody would use.

## 8. Pluggable transports: the architecture

**pt-spec §2, "Architecture Overview"** —
[pt-spec.txt#L61](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L61)

A pluggable transport is a *separate program*, not a library. On the client
host it runs as a forward proxy that speaks SOCKS on a loopback address; the
application hands it a destination over SOCKS, and it applies whatever
obfuscation it implements before forwarding the traffic. On the bridge host a
matching reverse proxy undoes the obfuscation. The same section states that
every PT instance is either a client or a server, that all instances are
configured through standardised environment variables set at launch, that they
report status by writing to stdout, and that a PT client forward proxy must
support SOCKS4 or SOCKS5 and should prefer SOCKS5.

**pt-spec §3, "Specification"** —
[pt-spec.txt#L95](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L95)

That section opens with the lifecycle as a numbered workflow: the parent sets
the environment and forks the proxy; the proxy decides which specification
version to use and says so, or reports a version error and exits; it parses the
rest of the environment, reporting an environment error and exiting if it is
malformed; a client proxy validates any upstream proxy it was given; it
initialises its transports and reports the outcome of each; it then forwards
traffic until the parent signals shutdown.

The decoupling is the whole design. The obfuscation authors ship a binary and
need know nothing about Tor; the Tor client needs know nothing about the
obfuscation. The price is that the interface between them — environment in,
lines out, SOCKS across — is a real protocol with real failure modes, and
getting those failure modes right is most of what this step grades.

## 9. Transport names

**pt-spec §3.1, "Pluggable Transport Naming"** —
[pt-spec.txt#L144](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L144)

A transport name is a unique identifier and must be a valid C identifier: a
leading letter or underscore, then letters, digits and underscores, with no
length limit —
[pt-spec.txt#L149](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L149).
The section states the same rule as a regular expression —
[pt-spec.txt#L154](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L154).

So `obfs4`, `meek_lite`, `snowflake`, `webtunnel`, `_private`, `a0` and `Z_9`
are all well-formed, and `4obfs` (leading digit), `obfs-4` (hyphen), `obfs.4`
(dot), `obfs 4` (space), `obfs4!` and the empty string are not. The rule is
mechanical and it is the same rule at every entry point: the bridge line, the
configuration builder, and the transport-name type itself must all accept and
reject exactly the same strings, or a name that survives one path will be
rejected on another.

Alongside the pluggable names there has to be a way to say "no transport,
connect directly", because the same type is used for both cases and a direct
relay has to answer the question too. Arti spells the built-in transport four
ways on input — the empty string, `-`, `<none>`, and `bridge` — and always
writes it as `-`. Three of those spellings are deliberately *not* C identifiers,
so they can never collide with a real transport name.

The fourth, `bridge`, is a C identifier, and pt-spec §3.1 does not reserve it.
Arti reserves it anyway, and refuses it as a *transport* name in any letter
case, because the bridge-line grammar already uses the word `bridge` as the
optional leading keyword (section 3) and as the direct-connection spelling; a
transport actually called `bridge` could not be written down unambiguously.
This is an arti convention layered on top of the specification, not a
specification rule, and it produces the one asymmetry worth remembering: the
transport-identifier type parses `bridge` (as the built-in), while the
pluggable-name type refuses it.

## 10. The managed-proxy environment

**pt-spec §3.2, "Pluggable Transport Configuration Environment Variables"** —
[pt-spec.txt#L156](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L156)

All configuration is passed at launch time through environment variables. There
is no configuration file and no command channel: once the process is running,
the only thing the parent can change is that it can stop it.

**pt-spec §3.2.1, "Common Environment Variables"** —
[pt-spec.txt#L166](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L166)

* `TOR_PT_MANAGED_TRANSPORT_VER` lists the specification versions the *parent*
  supports, comma-delimited; a proxy must accept any well-formed list as long as
  a version it knows is in it —
  [pt-spec.txt#L171](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L171).
  The version defined by the document is `1` —
  [pt-spec.txt#L181](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L181).
  A parent that implements only version 1 sends exactly `1`.
* `TOR_PT_STATE_LOCATION` is an absolute path to a directory the proxy may use
  for state that persists across runs; it need not exist yet, and the proxy is
  expected to create it —
  [pt-spec.txt#L190](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L190).
  The proxy must confine its files to that path —
  [pt-spec.txt#L196](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L196).
  The variable name is `TOR_PT_STATE_LOCATION` —
  [pt-spec.txt#L188](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L188).
* `TOR_PT_EXIT_ON_STDIN_CLOSE` —
  [pt-spec.txt#L203](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L203)
  — tells the proxy that the parent will close its stdin to ask it to exit. A
  proxy must not treat a closed stdin as a termination signal unless this is set
  to `1` —
  [pt-spec.txt#L209](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L209).
  A parent that intends to use that shutdown mechanism therefore has to set it,
  and set it to exactly `1`.
* `TOR_PT_OUTBOUND_BIND_ADDRESS_V4` —
  [pt-spec.txt#L219](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L219)
  — and `TOR_PT_OUTBOUND_BIND_ADDRESS_V6` —
  [pt-spec.txt#L236](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L236)
  — name a source address for outgoing packets, for hosts with several
  interfaces. Both are optional, and both are omitted when unset rather than set
  to an empty value.

**pt-spec §3.2.2, "Pluggable Transport Client Environment Variables"** —
[pt-spec.txt#L254](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L254)

* `TOR_PT_CLIENT_TRANSPORTS` —
  [pt-spec.txt#L259](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L259)
  — is the comma-separated list of transports the client proxy should start —
  [pt-spec.txt#L261](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L261).
  Setting it is what makes this a client invocation rather than a server one.
  The order is the order the parent asked for, and the join is a bare comma with
  no spaces.
* `TOR_PT_PROXY` —
  [pt-spec.txt#L273](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L273)
  — names an upstream proxy the transport must route through. It is optional and
  must be *omitted* when there is none —
  [pt-spec.txt#L281](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L281).
  Omitted, not empty: the proxy decides whether to expect a `PROXY` reply by
  looking at whether the variable is present, so an empty value would desynchronise
  the two sides.

The absent-versus-empty rule is the one an implementation is most likely to get
wrong, and it is graded directly: with no proxy configured, the variable must
not appear in the child's environment at all.

## 11. The managed-proxy stdout protocol

**pt-spec §3.3, "Pluggable Transport To Parent Process Communication"** —
[pt-spec.txt#L403](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L403)

The proxy talks back by writing newline-terminated lines to stdout. The
metaformat is a keyword of ASCII alphanumerics, dashes and underscores,
optionally followed by space-separated arguments of any non-NUL, non-newline
characters —
[pt-spec.txt#L406](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L406).
The parent must ignore lines whose keyword it does not recognise —
[pt-spec.txt#L417](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L417).
That is the forward-compatibility hinge: a newer transport may emit keywords
this client has never heard of, and the correct response is to skip the line,
not to abort the launch.

**pt-spec §3.3.1, "Common Messages"** —
[pt-spec.txt#L420](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L420)

* `VERSION-ERROR <message>` says no version in the parent's list was usable —
  [pt-spec.txt#L433](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L433)
  — and the proxy must terminate after emitting it —
  [pt-spec.txt#L443](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L443).
  The message is conventionally `no-version`. From the parent's side this is an
  unsupported-version outcome, and it is final: the binary and the client have
  no specification version in common, and retrying changes nothing.
* `VERSION <v>` announces the version the proxy has chosen —
  [pt-spec.txt#L450](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L450)
  — and the version for the messages and environment this document defines is
  `1` —
  [pt-spec.txt#L457](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L457).
  A parent that offered only `1` and is answered with anything else is being
  told something impossible; that is a protocol violation, and the diagnostic
  should name the version that was offered back.
* `ENV-ERROR <message>` reports that the environment could not be parsed —
  [pt-spec.txt#L477](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L477)
  — and again the proxy must terminate —
  [pt-spec.txt#L486](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L486).
  Since the parent is the one that wrote that environment, an `ENV-ERROR` means
  one of the two sides has a bug; it is classified as a violation by the child,
  carrying the child's message so a human can see which variable it disliked.

**pt-spec §3.3.2, "Pluggable Transport Client Messages"** —
[pt-spec.txt#L493](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L493)

* `PROXY DONE` accepts the upstream proxy —
  [pt-spec.txt#L503](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L503).
  A parent that configured no upstream proxy has no business receiving it, and
  must treat it as a violation: the child is confirming something it was never
  asked to do, so the two sides disagree about the configuration. The same holds
  for a second `PROXY DONE`.
* `PROXY-ERROR <message>` says the upstream proxy is unusable —
  [pt-spec.txt#L508](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L508)
  — and the proxy must terminate immediately —
  [pt-spec.txt#L513](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L513).
  The parent reports it as a proxy failure and preserves the message, because
  only the child knows what was wrong with the URI.
* `CMETHOD <transport> <socks4|socks5> <address:port>` announces that one
  transport is up, which SOCKS dialect to speak to it, and where it is
  listening —
  [pt-spec.txt#L527](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L527).
  Two checks fall out of that line. The transport named must be one the parent
  asked for — a proxy announcing an unrequested transport is out of step with
  the parent, and the diagnostic **must** name it. And the dialect field has
  exactly two legal values; anything else is a violation, and its diagnostic
  **must** name the offending value, because the parent would otherwise have to
  guess how to speak to the listener. Both are graded by looking for that text,
  so "name it" is a requirement here rather than advice.
* `CMETHOD-ERROR <transport> <message>` says one requested transport could not
  start —
  [pt-spec.txt#L538](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L538).
  It is per-transport, so the report must name both the transport and the
  child's message.
* `CMETHODS DONE` ends initialisation —
  [pt-spec.txt#L551](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L551).
  At that moment the parent knows the final answer for every transport it asked
  about. Any transport with neither a `CMETHOD` nor a `CMETHOD-ERROR` is simply
  unsupported by this binary, and the parent should report the whole list of
  such transports rather than one of them.

Two notes in that section shape the parent's loop. Transports the proxy does
not recognise are ignored entirely and must *not* produce a `CMETHOD-ERROR`, so
a proxy may legitimately answer with nothing but `CMETHODS DONE` —
[pt-spec.txt#L562](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L562).
And the parent must accept `CMETHOD` and `CMETHOD-ERROR` lines in any order,
regardless of the order it listed transports in —
[pt-spec.txt#L567](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L567).
So the parent accumulates results into a map and only evaluates completeness at
`CMETHODS DONE`; it must not assume position.

**Timeout.** Nothing in pt-spec bounds how long a proxy may take to reach
`CMETHODS DONE`, and a binary that says nothing at all would otherwise hang the
client for ever. A managed-proxy manager therefore imposes its own deadline on
the whole startup conversation and abandons the child when it expires. That is
an implementation decision, not a specification rule — but it is graded, and it
is the *configured* value that has to be honoured, not a constant of your own
choosing: the manager reads the deadline from its parameters, and a hard-coded
one lets a silent proxy outlive it or fails with the wrong error. A silent
binary must produce a timeout outcome, and that outcome is classified as an
*external tool* failure that may be retried after waiting — unlike the protocol
violations above, which are the client's or the binary's fault and are never
worth retrying. A binary that cannot be spawned at all (missing file, not
executable) is likewise never retried: nothing about waiting will make the file
appear.

Log and status lines (pt-spec §3.3.4 and §3.3.5, the `LOG` and `STATUS`
keywords) are part of version 1 but carry no information the launch decision
needs. They are recognised and discarded at this step; see section 17.

## 12. Shutdown

**pt-spec §3.4, "Pluggable Transport Shutdown"** —
[pt-spec.txt#L687](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L687)

The recommended sequence is: the parent sets `TOR_PT_EXIT_ON_STDIN_CLOSE` at
launch —
[pt-spec.txt#L692](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L692)
— and, when it wants the proxy gone, closes the proxy's stdin and waits a
reasonable time before escalating to platform signals —
[pt-spec.txt#L698](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L698).
The proxy side of the bargain is to watch stdin and exit gracefully when it
closes —
[pt-spec.txt#L705](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L705).

This is why the environment variable is not decoration: a parent that does not
set it has no polite way to ask the child to stop, and a child process left
running after its parent exits is both a resource leak and, for a censored
user, a running obfuscation proxy that nobody is watching.

Only the parent's obligations are in scope here, and only lightly: launching a
transport that is already running must be harmless (a no-op, not a second
child), and dropping the manager must not leave the conversation half-finished.

## 13. Talking to the transport: SOCKS

Once a transport reports its listener, the client reaches a bridge by opening a
TCP connection to that loopback listener and performing a SOCKS handshake as the
*initiator*. This is the direction the client has not needed before: for eleven
steps it has been a SOCKS *responder*, accepting connections from applications.
Now it must also speak the client half.

**socks-extensions §1, "Overview"** —
[socks-extensions.txt#L13](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L13)

Tor works with SOCKS4, SOCKS4a and SOCKS5 —
[socks-extensions.txt#L21](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L21).
SOCKS4 addresses by IPv4 address only; SOCKS4a extends it to carry a hostname;
SOCKS5 handles addresses and names natively.

**socks-extensions §1.1, "Extent of support"** —
[socks-extensions.txt#L31](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L31)

The relevant detail for this step is the authentication-method numbering:
method `[00]` is "no authentication required" and method `[02]` is
username/password —
[socks-extensions.txt#L46](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L46).
Those two numbers are the ones a client offers a transport.

The byte-level formats themselves come from RFC 1928 (SOCKS5), RFC 1929
(username/password subnegotiation) and the SOCKS4/4a memos. Those documents are
not part of the torspec corpus, so the following is stated here from the
protocol definitions rather than cited to a local file; the conformance suite
asserts these byte strings directly, deliberately not against the
implementation's own encoder.

**Version 5.** The client speaks first, with a greeting: the version byte `5`, a
count, and that many method bytes. Offering only no-authentication is
`05 01 00`; offering username/password as well is `05 02 02 00` — the preferred
method first, with no-authentication retained as a fallback. The server answers
with two bytes: the version and the method it selected.

If the server selects method `2`, the client sends the RFC 1929 subnegotiation:
a version byte `01`, then the username as a one-byte length followed by that
many bytes, then the password the same way. Note the leading `01` is the
*subnegotiation* version, not the SOCKS version.

The request that follows is the version byte `5`, the command (`1` for
CONNECT), a reserved zero byte, an address-type byte, the address, and the port
as a big-endian `u16`. Address type `1` is a four-byte IPv4 address; address
type `3` is a name written as a one-byte length followed by that many bytes.
Sending a name rather than an address is what keeps the client from resolving
the bridge's hostname locally.

The final reply has the same shape as the request, with a status byte in place
of the command. Status `0x00` is success. When it arrives the handshake is
finished, the reply's status and bound address are available to the caller, and
the underlying stream becomes the tunnel.

**Version 4.** Everything is one message: the version byte `4`, the command, the
port as a big-endian `u16`, four address bytes, the user ID, and a NUL
terminator. There is no method negotiation and no separate authentication step,
so there is exactly one thing to send and then one thing to read.

**Version 4a** is signalled inside that message: the client writes an address of
the form `0.0.0.x` with a nonzero final octet — an address that can never be a
real destination — and then appends the NUL-terminated hostname after the
NUL-terminated user ID. A version-4 server that does not understand 4a will try
to connect to the impossible address and fail, which is the intended behaviour.
The memo leaves the octet free, but arti pins it: **the sentinel written is
`0.0.0.1`**, the four bytes `00 00 00 01`, and the conformance suite asserts the
4a message byte-for-byte against that value. Any other nonzero octet conforms to
the memo and still fails the assertion, so write `0.0.0.1`.

The version-4 reply is eight bytes: a zero byte, a status byte, the bound port
and the bound address. Status `0x5A` is "request granted" and `0x5B` is
"request rejected or failed". A client that supports both dialects normalises
those onto the version-5 status codes so that callers see one status type: `5A`
becomes success, `5B` becomes general failure.

**Truncation is not failure.** A handshake driven from a socket is fed whatever
bytes have arrived so far, which may be part of a message. A short input must be
reported as *truncated* — distinct from a protocol error — and must consume
nothing, so that the caller can read more and re-feed the whole buffer. Folding
truncation into the error path would turn every slow network into a handshake
failure.

**But the client speaks first, so the opening call has nothing to read.** In
both dialects the initiator's first act is to send, not to receive: the caller
starts the handshake by invoking it with an *empty* input, and in that initial
state the handshake must ignore the input entirely and emit its opening message.
An empty slice at the start is not truncation. Truncation is a property of a
handshake that is *waiting for the proxy* and has received only part of what it
is waiting for; a uniform "insufficient input ⇒ truncated" rule would deadlock
the handshake before it ever sent a byte.

## 14. Per-connection transport arguments

**pt-spec §3.5, "Pluggable Transport Client Per-Connection Arguments"** —
[pt-spec.txt#L716](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L716)

Some transports need per-connection parameters — a bridge's certificate, a
shared secret, a mode flag. These are exactly the `key=value` words at the end
of a bridge line (section 3). There is no side channel for them: on the client
side they travel in the authentication fields of the SOCKS handshake —
[pt-spec.txt#L723](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L723).

The encoding has three steps. First, each `key=value` pair is escaped so that
backslash, equals and semicolon characters are preceded by a backslash —
[pt-spec.txt#L726](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L726).
Second, the escaped pairs are concatenated, separated by semicolons. Third, the
result is transmitted using the authentication mechanism of the SOCKS dialect in
use: for SOCKS4, in the user-ID field of the CONNECT request —
[pt-spec.txt#L740](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L740)
— and for SOCKS5, by negotiating username/password authentication and splitting
the encoded list across the username and password fields —
[pt-spec.txt#L743](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L743).
When the encoded list fits in under 255 bytes it all goes in the username, and
the password is set to a single NUL byte rather than left empty —
[pt-spec.txt#L747](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L747).
Each field is length-prefixed with a single byte, so the two fields together
cap the encoded list at 510 bytes; beyond that the settings cannot be sent.

This is also the reason the settings themselves are validated when they are
stored, not when they are sent. A key containing `=` or whitespace, or a value
containing whitespace, cannot survive the bridge-line round trip — the line is
split on whitespace and each word on its first `=` — so such a pair is refused
at the point it is pushed, and the rejected push leaves the target unchanged
rather than half-updated. The same validation applies whether the pairs arrive
through the builder, through a bridge line, or as a whole settings list.

## 15. Extra-info `transport` lines and BridgeDB

**dir-spec §2.1.2, "Extra-info document format"** —
[dir-spec.txt#L958](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L958)

A relay advertises the transports it runs in its extra-info document, on a
`transport` line naming the transport, the address and port it listens on, and
an optional argument list; one document may not repeat the same transport name —
[dir-spec.txt#L1374](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1374).

**bridgedb-spec §1.3, "Parsing extra-info documents"** —
[bridgedb-spec.txt#L109](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L109)

This is how the distribution service learns that a bridge supports a transport
at all —
[bridgedb-spec.txt#L111](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L111)
— by reading those `transport` lines —
[bridgedb-spec.txt#L122](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L122)
— whose arguments are comma-separated `k=v` pairs —
[bridgedb-spec.txt#L129](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/bridgedb-spec.txt#L129).
The service then rewrites them, space-separated, into the bridge line it hands
the user (section 3).

The client end of this pipeline is the bridge line and nothing else. A client
does not parse extra-info documents, does not talk to the distribution service,
and does not discover transports: it is told, by the line, which transport to
use and with which parameters. This section is here so that the `transport`
line is recognised as the *origin* of the bridge line's tail, and not mistaken
for something the client must implement.

## 16. Anti-censorship properties and redacted logging

**pt-spec §4, "Anonymity Considerations"** —
[pt-spec.txt#L751](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/pt-spec.txt#L751)

That section asks implementors to avoid leaking personally identifying
information — not writing client IP addresses to disk, not leaking DNS lookups
except where necessary, and failing closed when an upstream proxy is
configured. It closes with the point that matters most here: some obfuscation
mechanisms depend on the *server's* address and port staying confidential, so
clients must protect server-side information too.

That sentence is the whole justification for redacted logging of bridges. A
bridge's value is that the censor does not know it exists; a log file, a bug
report or a screenshot that contains the bridge address destroys that value
permanently, and unlike a leaked password an address cannot be rotated without
disrupting every other user of that bridge.

The step-08 safe-logging model — sensitive values scrubbed entirely unless the
operator opts in — is the blunt instrument. Redaction is the sharp one: it keeps
enough of a value to correlate two log lines with each other, and drops enough
that the value cannot be reconstructed. The shape of each redacted rendering
follows from that trade-off:

* a socket address keeps its leading field and its port. The port is not secret
  (it is often 443 or 9001 and carries little information), while the leading
  octet or group is enough to tell two different bridges apart in a log without
  locating either.
* a hostname keeps a two-character prefix and its port. Two characters
  distinguish the bridges a given user has configured; they do not identify a
  host on the Internet.
* an identity keeps a two-character prefix and its sigil — the `$` that marks an
  RSA fingerprint survives, because knowing *which kind* of key is being talked
  about is diagnostic, not sensitive.
* an absent transport target has nothing to hide and is the same placeholder
  redacted or not.
* a channel target keeps its (redacted) connection point and only its *first*
  identity, untagged. Printing every identity would defeat the prefix truncation,
  since several two-character prefixes of the same relay are together nearly as
  identifying as one whole key.
* a pluggable target still names its transport in the clear. The transport name
  is public knowledge — there are a few dozen of them and they are listed in
  documentation — and suppressing it would make logs unreadable without
  protecting anything.

Redaction is presented as a rendering choice rather than a wrapper the caller
must remember: every type that can name a bridge offers both renderings, and a
single flag chooses between them, so a call site can be made safe without being
rewritten.

## 17. What this step does not implement

The specifications above permit a good deal more than this step builds. Listed
explicitly so that none of it is mistaken for a requirement:

* **The server side of pt-spec.** Sections 3.2.3 and 3.3.3 define the
  server-side environment (`TOR_PT_SERVER_TRANSPORTS`, `TOR_PT_SERVER_BINDADDR`
  and friends) and the `SMETHOD` / `SMETHODS DONE` messages. This is a client;
  it never launches a server-side transport, and the server messages are not
  recognised.
* **The extended ORPort** (`ext-orport-spec.txt`), which a bridge uses to let a
  local transport pass connection metadata to the Tor process. It is a
  bridge-side mechanism and has no client half.
* **`LOG` and `STATUS` messages** (pt-spec §3.3.4 and §3.3.5). They are part of
  version 1 and must not break the launch conversation, but nothing is done with
  them at this step beyond ignoring them — as the unknown-keyword rule in §3.3
  requires for anything else the client does not consume.
* **Version negotiation beyond `1`.** `TOR_PT_MANAGED_TRANSPORT_VER` may carry a
  comma-separated list; this client offers exactly `1` and refuses any other
  answer. Implementing a list would be legal and would buy nothing.
* **Unmanaged transports.** A transport that is already running and merely told
  to the client — no child process, no environment, no stdout protocol — is a
  configuration form that exists in C Tor. There is no such configuration here;
  every pluggable transport is a managed child.
* **Upstream proxying of the client's own traffic.** `TOR_PT_PROXY` is passed
  through to the transport when a proxy URI is configured, and the `PROXY DONE`
  and `PROXY-ERROR` replies are handled. The client does not itself route
  anything through an upstream proxy, and no configuration is wired up to set
  the URI.
* **Outbound bind addresses.** `TOR_PT_OUTBOUND_BIND_ADDRESS_V4` and `_V6` are
  accepted as launch parameters and set when present; nothing exercises them.
* **Bridge lines without an identity.** Proposal `125-bridges.txt` (§3.1)
  permits a bridge line consisting of only an address and port, and contemplates
  a future form consisting of only a fingerprint. Neither is accepted here: an
  RSA identity is mandatory and an address (or the explicit "no address"
  spelling) is required.
* **The bridge authority route.** Proposal `125-bridges.txt` (§3.4) offers
  fetching a bridge descriptor from the bridge authority by fingerprint, gated
  on `UpdateBridgesFromAuthority`. Of the two routes it describes, the direct
  query to the bridge itself is the one a client would use — and the next bullet
  puts that one out of scope here as well, so neither route is built at this
  step.
* **Fetching a bridge descriptor over the network.** Section 7 describes the
  direct route in full: open a channel to the bridge, tunnel a directory request
  for dir-spec's general-use `/tor/server/authority` resource over it, read the
  descriptor back, and retry on a randomized backoff whose base delay for bridge
  descriptors is 10800 seconds once a bridge is usable. None of that is built
  here. What this step builds is the *storage and verification* half of that
  same section — the `BridgeDescList` keyed by the bridge line the descriptor
  was fetched for, the slot that may hold a failure instead of a descriptor, and
  the provider that hands the map out and announces that something changed.
  `TEST_INTERFACE.md` §8 names no downloader, no directory client and no
  runtime, so no public item in the contract reaches the download path and no
  group grades it. Section 7 states the route and the constant so that a
  descriptor arriving from anywhere is understood, not because anything here
  goes and gets one.
* **Caching and expiring bridge descriptors.** Both follow the download: with no
  fetch there is nothing to cache and nothing to age out. The `BridgeDescList`
  this step grades is an in-memory map the caller fills, with no persistence, no
  clock and no eviction; `TEST_INTERFACE.md` §8 names no cache type, no store
  and no expiry method, so nothing in the contract reaches them and no group
  grades them. It is also why the `published` timestamp of section 6 is read
  back and compared against a fixed expected value and never against the wall
  clock.
* **The bridge guard universe.** Section 2 describes the model — bridges as an
  alternative universe to the consensus, with their own sample and their own
  filter — and this step grades the types that model implies: a `BridgeConfig`
  that is a `ChanTarget` in its own right (`TEST_INTERFACE.md` §4 and §5) and
  the descriptor provider the guard manager would read (§8). Selecting a guard
  over a bridge-derived set is the step after that, and it needs exactly the
  descriptors the two bullets above do not obtain. Every item the contract lists
  from `tor_guardmgr` is a `tor_guardmgr::bridge` type; no guard sample, no
  filter and no selection entry point is named, so nothing reaches bridge guard
  selection and no group grades it.
* **Bridge network statuses and extra-info documents.** bridgedb-spec §1.1 and
  §1.3 describe documents the distribution service consumes. A client parses
  neither; it parses router descriptors only.
* **Bridge distribution channels** — the HTTPS and email distributors of
  bridgedb-spec §2 and §3, moat, and `gettor-spec.txt`. Getting a bridge line to
  the user is out of scope; the client's story starts once the line exists.
* **Time-bounded descriptor validation.** Router descriptors carry a validity
  window and the machinery to check it is present, but the graded path
  deliberately assumes timeliness rather than checking it against the wall
  clock, so that nothing in the suite depends on the current date.
* **Bridge-specific guard state persistence.** guard-spec §2 requires a separate
  persistent state instance for the `UseBridges` case. The model is described in
  section 2 because it explains the shape of the bridge types; the persistence
  itself is not observable through anything named in `TEST_INTERFACE.md`.
* **Observing the shutdown handshake.** Section 12 describes the polite exit:
  the parent sets `TOR_PT_EXIT_ON_STDIN_CLOSE` at launch, then closes the child's
  stdin when it wants the child gone. The parent's half is built, and the suite
  asserts that the variable reaches the child with the value `1` — but the
  handshake itself is not graded. Under scope decision S5 the far end is a
  first-party mock that exits of its own accord the moment its script runs out,
  so "the child went away because its stdin closed" and "the child went away
  because it was finished" cannot be told apart from outside; and no public
  method on `PluggableTransport` reports the child's fate either way. The
  environment variable's presence is the whole of what is checkable here.
* **Sending per-connection transport arguments.** Sections 13 and 14 describe how
  a `key=value` settings list is escaped, joined with semicolons and carried to
  the transport in the authentication fields of the SOCKS handshake. This step
  builds, validates and stores those settings — that much is graded, because it
  happens on a bridge line, before any connection exists — but nothing sends
  them. Sending them requires a real transport listening on a real SOCKS port,
  which S5 puts out of scope; the mock managed proxy never opens a socket.
* **Splitting a long argument list across the username and password.** The
  per-field rules of section 14 — everything in the username while the encoded
  list stays under 255 bytes, a single NUL byte in the password rather than an
  empty one, a spill into the password past that, and no way to send more than
  510 bytes — apply only on the wire, which the previous bullet puts out of
  reach. They are written down so that the 255-byte figure is not mistaken for a
  bound this step enforces: nothing here measures a settings list, and the
  settings validation described in section 14 rejects pairs on their *shape* —
  a key containing `=` or whitespace, a value containing whitespace — never on
  their length.
