# Specification notes — step 05

First-party prose stating the normative protocol and design facts this step is graded against. Every fact is cited to a numbered section of the Tor specifications at commit `1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28) with a line-anchored URL. No specification body text is reproduced; the citations are to section headings and the prose below is written for this workspace.

## TLS handshake and connection establishment

A Tor client reaches a relay by opening a TCP connection and then performing a TLS handshake with it. Only after the TLS handshake completes does the client begin the Tor link protocol (version negotiation, certificate exchange, authentication, and NETINFO).

**tor-spec.txt §2 (Connections), L268:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L268

The TLS implementation is not fixed by the protocol: which ciphersuites are offered and which library performs the handshake are implementation choices, so a client may be built with more than one TLS backend and must interoperate with the network either way.

**tor-spec.txt §2.1 (Picking TLS ciphersuites), L422:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L422

**tor-spec.txt §2.2 (TLS security considerations), L478:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L478

Once the TLS handshake has succeeded, the client can obtain the certificate the peer presented during that handshake. The link protocol's CERTS cells carry the relay's signing and link certificates and are validated against the identity keys the client already knows.

**tor-spec.txt §4.2 (CERTS cells), L688:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L688

**tor-spec.txt §4 (Negotiating and initializing connections), L603:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L603

## Bootstrap progress

A client cannot build useful circuits until it has a live consensus and enough relay descriptors. Bootstrapping therefore has two observable phases: reaching the Tor network at the transport level, and obtaining directory information.

**dir-spec.txt §1 (Outline), L203:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L203

**dir-spec.txt §5 (Client operation), L3680:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3680

The transport phase is described by two booleans: whether a TCP connection to any relay has succeeded, and whether a **Tor channel handshake** over such a connection has completed. Neither known, the first only, and both correspond to no progress, half progress, and full progress respectively. Before any successful TCP connection the client is still "connecting to the internet"; once TCP works but no channel has completed, it is "handshaking with Tor relays". (The second flag tracks the completed channel, not the TLS handshake: TLS is a layer underneath, and a connection whose TLS wrapped but whose relay never answered is not progress towards a circuit.)

The directory phase is described by whether a usable consensus has been obtained and whether descriptor material is present. A client with no consensus yet is still fetching one.

A client may therefore be unable to make forward progress for identifiable reasons — it cannot open TCP connections at all, or it can open them but TLS keeps failing, which suggests the network is filtering Tor traffic. Those two cases are distinguished so that the user can be told which one applies.

**dir-spec.txt §5.1 (Downloading network-status documents), L3685:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3685

**dir-spec.txt §5.2 (Downloading server descriptors or microdescriptors), L3760:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3760

**dir-spec.txt §5.5 (Retrying failed downloads), L3890:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3890

## Stream isolation

Streams leaving a client may be multiplexed onto shared circuits and also share a circuit with other streams, or they may be isolated so that unrelated activities never share a circuit. Isolation limits how much a single malicious exit can correlate separate activities of the same user.

**tor-spec.txt §6 (Application connections and stream management), L1645:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1645

An isolation token identifies a group of streams permitted to share circuits. Streams carrying different tokens must not be multiplexed onto the same circuit. A client may attach an explicit token to a request, or operate in a mode that mints a fresh token for every stream so that no two streams ever share a circuit by default.

**tor-spec.txt §6.2 (Opening streams and transferring data), L1756:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1756

**tor-spec.txt §6.1 (Relay cells), L1647:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1647

## Circuit construction and preemption

Circuits are built through a sequence of CREATE/CREATED handshakes, one hop at a time, extending the circuit by one relay per step.

**tor-spec.txt §5.1 (CREATE and CREATED cells), L983:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L983

**tor-spec.txt §5.3 (Creating circuits), L1378:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1378

To hide circuit-construction latency behind user think time, a client may build circuits to destinations it predicts the user will want before any request arrives. Predictions are drawn from the ports the user has recently connected to; a small default set of common ports is used when there is no history. A preemptive circuit that goes unused is reclaimed after a configured lifetime.

Circuits are also retired after a configured maximum age measured from first use, so that no circuit carries traffic for unbounded time and accumulates a correlatable volume.

**tor-spec.txt §5.2 (Setting circuit keys), L1323:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1323

**tor-spec.txt §5.4 (Tearing down circuits), L1474:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1474

**tor-spec.txt §7.3 (Circuit-level flow control), L2053:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2053

## Reconfiguration

Some configuration takes effect only at startup and cannot be altered on a running client. A reconfiguration request that would touch such a field is refused, and the refusal names the field so the caller can tell which one was rejected. Reconfiguration can be requested in a mode that fails as a whole on the first locked field, a mode that only checks whether it would succeed, and a mode that applies what it can and merely warns about what it cannot.

**tor-spec.txt §0.1 (Notation and encoding), L95:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L95

**dir-spec.txt §1.2 (Document meta-format), L260:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L260

## Subprotocol versioning

The link protocol carries a version-negotiation step before any certificates are exchanged, and the versions a relay supports constrain what the client may send. This step does not grade version negotiation, but the connection status described above is reported before it completes.

**tor-spec.txt §9 (Subprotocol versioning), L2186:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2186

**tor-spec.txt §9.1 ("Link"), L2239:**
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L2239
