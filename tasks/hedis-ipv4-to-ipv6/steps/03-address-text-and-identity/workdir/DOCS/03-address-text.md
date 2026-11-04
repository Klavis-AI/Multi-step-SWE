# The text of an address, and when two texts are one address — the reference for this workspace

This note collects the parts of the published standards that the work in this
workspace rests on. It states what the standards say; it does not state what
this repository must do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **RFC 4291 — IP Version 6 Addressing Architecture**, §2.2 for the text forms
  an address may be written in. <https://www.rfc-editor.org/rfc/rfc4291>
* **RFC 5952 — A Recommendation for IPv6 Address Text Representation.**
  <https://www.rfc-editor.org/rfc/rfc5952>
* **RFC 3986 — Uniform Resource Identifier (URI): Generic Syntax**, §3.2.2 for
  the host component and the bracketed literal.
  <https://www.rfc-editor.org/rfc/rfc3986>
* **POSIX.1-2017**, the `inet_ntop`, `inet_pton`, `getaddrinfo` and `getnameinfo`
  pages. <https://pubs.opengroup.org/onlinepubs/9699919799/>
* The C library's own manual pages, `man 3 inet_ntop`, `man 3 inet_pton` and
  `man 3 getnameinfo`, which are installed in this container.

## 1. One address, many spellings (RFC 4291 §2.2)

RFC 4291 §2.2 gives three conventional forms for writing an IPv6 address:

1. `x:x:x:x:x:x:x:x`, where each `x` is one to four hexadecimal digits of a
   16-bit field. §2.2 notes that leading zeros in a field may be omitted, so
   `2001:0db8:0000:0000:0000:0000:0000:0001` and
   `2001:db8:0:0:0:0:0:1` are the same address written two ways.
2. `::` standing for one **or more** groups of 16 bits of zeros, allowed to
   appear **once** in an address. `0:0:0:0:0:0:0:1` may therefore be written
   `::1`, and also `::0:1`, and also `0::1`.
3. The mixed form `x:x:x:x:x:x:d.d.d.d`, whose last 32 bits are written as four
   decimal octets — the form used for addresses like `::ffff:192.0.2.1`.

The consequence §2.2 leaves implicit and RFC 5952 §1 states outright is the one
that matters here: **a single address has many valid text representations**, and
two programs that both write it correctly can write it differently.

## 2. The canonical form (RFC 5952 §4)

RFC 5952 §4 narrows the forms of §2.2 to exactly one per address, so that the
text can be compared, sorted, logged and looked up as a string. Its rules, each
stated in §4 as a MUST for a *producer* of text:

* **§4.1 — leading zeros are suppressed.** `2001:0db8::0001` is written
  `2001:db8::1`. A field of all zeros is written as the single character `0`.
* **§4.2.1 — `::` is used to its maximum capability.** A run of zero fields that
  could be shortened is shortened; `2001:db8:0:0:0:0:0:1` is written
  `2001:db8::1` and not `2001:db8:0:0:0:0:0:1` or `2001:db8::0:0:1`.
* **§4.2.2 — `::` is not used for a *single* zero field.** `2001:db8:0:1:1:1:1:1`
  stays as it is; writing it `2001:db8::1:1:1:1:1` is forbidden, because the
  reader cannot then tell how many fields the `::` stood for.
* **§4.2.3 — when there is a choice, the longest run is shortened**, and when two
  runs are the same length, the **first** one is.
* **§4.3 — lower case.** The hexadecimal digits `a` through `f` are written in
  lower case.

§5 keeps the mixed `d.d.d.d` form of §2.2 for the addresses it belongs to —
IPv4-mapped and IPv4-compatible — and not for others.

RFC 5952 §4 is a rule for **producing** text. §1 and §4 are equally clear that a
program must go on **accepting** every form of RFC 4291 §2.2 on input: the
recommendation narrows what is written, never what is read.

### What this platform's C library already does

`inet_ntop(AF_INET6, …)` on this container produces the §4 form. Measured, not
assumed — each of these was parsed with `inet_pton` and printed back:

```
0:0:0:0:0:0:0:1       -> ::1                  (§4.2.1, §4.1)
2001:0db8::0001       -> 2001:db8::1          (§4.1)
2001:DB8::1           -> 2001:db8::1          (§4.3)
2001:db8:0:0:0:0:0:1  -> 2001:db8::1          (§4.2.1)
1:0:2:3:4:5:6:7       -> 1:0:2:3:4:5:6:7      (§4.2.2 — one zero field, not `::`)
1:0:0:0:2:0:0:0       -> 1::2:0:0:0           (§4.2.3 — equal runs, the first)
0:0:0:1:0:0:0:0       -> 0:0:0:1::            (§4.2.3 — the longer run)
```

`inet_pton(AF_INET6, …)` accepts every form of §2.2. `getnameinfo()` with
`NI_NUMERICHOST` and `getaddrinfo()` with `AI_NUMERICHOST` are the same pair of
conversions reached through the protocol-independent interface, and they carry
the family in the `sockaddr` rather than in the caller's code.

## 3. An address and a port in one string (RFC 3986 §3.2.2)

RFC 3986 §3.2 writes an authority as `host [ ":" port ]`, so a colon separates
the host from the port. An IPv6 address written by §2.2 contains colons of its
own, and the two uses cannot be told apart.

§3.2.2 resolves it with the `IP-literal` production:

```
host       = IP-literal / IPv4address / reg-name
IP-literal = "[" ( IPv6address / IPvFuture  ) "]"
```

An IPv6 address in a host position is therefore **enclosed in square brackets**,
and the port follows the closing bracket:

```
[2001:db8::1]:6379
[::1]:26379
```

§3.2.2 explains why the brackets are required and not optional: they are what
lets a parser find the delimiter between host and port. An IPv4 address or a
registered name is **not** bracketed — `192.0.2.1:6379` — so the bracket is also
how a reader tells the two apart.

The parsing rule that follows from the grammar: if the string begins with `[`,
the host is what lies up to the matching `]` and the port is what follows a `:`
after it; otherwise the host ends at the **last** `:` in the string. Splitting on
the first colon reads `::1:6379` as the host `` and the port `:1:6379`.

## 4. When two texts are the same address

RFC 4291 §2.2 and RFC 5952 §1 together say what identity means for an address:
the text is a representation, and the address is the 128 bits it represents.
`::1`, `0:0:0:0:0:0:0:1` and `0::1` are one address written three ways, and a
program that compares them as strings finds three different addresses.

The conversion that answers the question is the one in §2: parse each text with
`inet_pton()` (or `getaddrinfo()` with `AI_NUMERICHOST`) and compare the
resulting bytes — 16 of them for `AF_INET6`, 4 for `AF_INET`. Comparing the
**canonical text** of each, from `inet_ntop()`, answers it equally well, because
§4 gives each address exactly one canonical text. What does not answer it is
`strcmp()` or `strcasecmp()` on the text as it arrived.

Two addresses of different families are never the same address, whatever their
texts look like.

## 5. Where this repository's own interface is documented

The behaviour of the programs in this workspace — the configuration directives
they read, the commands they answer and the way they are built — is documented
by the repository itself: `README.md`, the annotated `redis.conf` and
`sentinel.conf`, and the test suite under `tests/`. Those files are the
authority on what this tree already does.
