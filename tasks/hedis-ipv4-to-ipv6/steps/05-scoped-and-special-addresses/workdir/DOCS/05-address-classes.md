# What an address *is*, and the classes it can belong to — the reference for this workspace

This note collects the parts of the published standards that the work in this
workspace rests on. It states what the standards say; it does not state what
this repository must do. The requirements are in the step instruction.

The documents are public. Look them up in full if you want the exact wording:

* **RFC 4291 — IP Version 6 Addressing Architecture**, §2.4 for the address
  types and how one is told from another, §2.5.2 the unspecified address,
  §2.5.3 the loopback address, §2.5.5.1 the IPv4-**compatible** form, §2.5.5.2
  the IPv4-**mapped** form, §2.5.6 link-local, §2.7 multicast.
  <https://www.rfc-editor.org/rfc/rfc4291>
* **RFC 4193 — Unique Local IPv6 Unicast Addresses**, for `fc00::/7`.
  <https://www.rfc-editor.org/rfc/rfc4193>
* **RFC 3879 — Deprecating Site Local Addresses**, for `fec0::/10`.
  <https://www.rfc-editor.org/rfc/rfc3879>
* **RFC 4007 — IPv6 Scoped Address Architecture**, §6 for what a zone is and
  §11.2 for the `<address>%<zone_id>` text notation.
  <https://www.rfc-editor.org/rfc/rfc4007>
* **RFC 5952 §5**, for which addresses are printed in the mixed
  `x:x:x:x:x:x:d.d.d.d` form. <https://www.rfc-editor.org/rfc/rfc5952>
* **RFC 6890 — Special-Purpose IP Address Registries**, and the IANA registries
  it defines, for the IPv4 side of the same question.
  <https://www.rfc-editor.org/rfc/rfc6890>
* The C library's own manual page `man 7 ipv6`, and `<netinet/in.h>`, both
  installed in this container.

## 1. The classes, and the prefix each is defined by

RFC 4291 §2.4 says the type of an address "is identified by the high-order bits
of the address", and gives the table. Adding the two documents that changed it
since, and the IPv4 equivalents from RFC 6890:

| Class | IPv6 prefix | IPv4 equivalent | Defined in |
|---|---|---|---|
| Unspecified | `::/128` | `0.0.0.0/32` | RFC 4291 §2.5.2, RFC 6890 |
| Loopback | `::1/128` | `127.0.0.0/8` | RFC 4291 §2.5.3, RFC 6890 |
| Link-local unicast | `fe80::/10` | `169.254.0.0/16` | RFC 4291 §2.5.6, RFC 3927 |
| Unique-local unicast | `fc00::/7` | (no equivalent; RFC 1918 is the nearest) | RFC 4193 |
| Site-local unicast | `fec0::/10` — **deprecated** | — | RFC 3879 |
| Multicast | `ff00::/8` | `224.0.0.0/4` | RFC 4291 §2.7 |
| Documentation | `2001:db8::/32` | `192.0.2.0/24` and two more | RFC 3849, RFC 5737 |
| Global unicast | everything else | everything else | RFC 4291 §2.5.4 |

Two consequences RFC 4291 §2.4 leaves implicit:

* The class is a property of the **value**. `fe80::1`,
  `fe80:0:0:0:0:0:0:1` and `fe80::1%eth0` are the same class because the first
  ten bits are the same, not because they look alike; an address never seen
  before is classified correctly the first time.
* There is **no broadcast address** in IPv6 (§2.7 says so outright); its role is
  taken by multicast. So "a group of interfaces" and "one interface" is the
  distinction that survives, and it is the one that decides whether an address
  can name a peer.

## 2. The two ways an IPv4 address is embedded, and why only one is current

RFC 4291 gives two formats that carry a 32-bit IPv4 address inside a 128-bit
one, and they are not interchangeable:

* **§2.5.5.2, IPv4-mapped**: `::ffff:a.b.c.d`, i.e. 80 zero bits, 16 one bits,
  then the address. This is the form "used to represent the addresses of IPv4
  nodes as IPv6 addresses". It is the address it embeds, seen through an IPv6
  API.
* **§2.5.5.1, IPv4-compatible**: `::a.b.c.d`, i.e. 96 zero bits then the
  address. §2.5.5.1 says in its own words that this format is **deprecated**,
  because the transition mechanism that used it is no longer supported. It is
  not the IPv4 address it looks like, and it is not a current unicast address.

RFC 5952 §5 says the mixed `d.d.d.d` tail is for exactly these two forms and
"should not be used" otherwise — which is why a NAT64 address does not get one.

## 3. Zones, and the `%` notation (RFC 4007)

RFC 4007 §5 says a non-global address is only meaningful inside a *zone*, and
§6 says an implementation must let an application say which zone it means. §11.2
gives the text notation: `<address>%<zone_id>`, where the `zone_id` is a string
whose meaning is the implementation's — on this platform an interface name, or
its numeric index.

The parts that matter for code:

* The zone is **part of the address**, not an annotation on it. §11.2's own
  wording is that the notation identifies an address "unambiguously" only with
  the zone attached, so two texts that differ only in zone are two addresses.
* A zone identifier is only meaningful for an address whose scope is smaller
  than global, which in practice means link-local.
* `getaddrinfo`/`getnameinfo` on this platform handle the notation directly and
  `inet_pton` does not — it has nowhere to put the zone. Measured:

```
inet_pton("fe80::1%lo")  = 0          (0 is "not a valid address in this family")
inet_pton("fe80::1")     = 1
getaddrinfo("fe80::1%lo", AI_NUMERICHOST) -> sin6_scope_id = 1
getnameinfo(that, NI_NUMERICHOST)         -> fe80::1%lo
```

  So a parser built on `inet_pton` alone cannot see a zone at all, and one built
  on `getaddrinfo` gets it in a separate field of the socket address rather than
  in the 16 bytes.

## 4. What this platform already answers, measured

`<netinet/in.h>` ships the `IN6_IS_ADDR_*` macros. Each row below was produced
by parsing the text with `inet_pton`, asking every macro, and printing the
address back with `inet_ntop`:

```
text                   loop unspec ll   sl  ula  mcast  v4map  v4compat   inet_ntop
::1                    1    0      0    0   0    0      0      0          ::1
::                     0    1      0    0   0    0      0      0          ::
fe80::1                0    0      1    0   0    0      0      0          fe80::1
fe80:0:0:0:0:0:0:1     0    0      1    0   0    0      0      0          fe80::1
fec0::1                0    0      0    1   0    0      0      0          fec0::1
fd00::1                0    0      0    0   1    0      0      0          fd00::1
fc00::1                0    0      0    0   1    0      0      0          fc00::1
ff01::1                0    0      0    0   0    1      0      0          ff01::1
ff02::1                0    0      0    0   0    1      0      0          ff02::1
ff0e::1                0    0      0    0   0    1      0      0          ff0e::1
2001:db8::1            0    0      0    0   0    0      0      0          2001:db8::1
::ffff:127.0.0.1       0    0      0    0   0    0      1      0          ::ffff:127.0.0.1
::ffff:7f00:1          0    0      0    0   0    0      1      0          ::ffff:127.0.0.1
::127.0.0.1            0    0      0    0   0    0      0      1          ::127.0.0.1
::1.2.3.4              0    0      0    0   0    0      0      1          ::1.2.3.4
64:ff9b::1.2.3.4       0    0      0    0   0    0      0      0          64:ff9b::102:304
```

Three things to take from it:

* The `ula` column is **not** a library macro — there is no `IN6_IS_ADDR_ULA` in
  this libc, and the column above was computed as `(a.s6_addr[0] & 0xfe) ==
  0xfc`. The library covers some of the classes of §1 and not all of them.
* `IN6_IS_ADDR_SITELOCAL` exists and answers `1` for `fec0::1`, even though
  RFC 3879 deprecated that prefix. A macro existing is not the same as the
  class being current.
* `inet_ntop` already writes the mixed form for both embedded-IPv4 formats and
  for nothing else — `::ffff:7f00:1` comes back `::ffff:127.0.0.1`, and the
  NAT64 prefix comes back with a hexadecimal tail.

## 5. What this container can and cannot show

Measured on the container this workspace runs in:

* `lo` carries `::1/128` and that is the whole of it — `ip -o -6 addr` returns
  exactly one line. `eth0` has **no IPv6 address at all**.
* There is therefore no link-local and no global IPv6 address to connect to,
  and `CAP_NET_ADMIN` is not held, so one cannot be added.
* A scoped address can still be written, parsed, stored, printed, gossiped and
  read back — all of that is text and value handling, and none of it needs the
  address to be reachable.

So a question of the form "does classifying this address correctly change what
the program does" is answerable here, and a question of the form "can the
program reach `fe80::1%eth0`" is not.

## 6. Where this repository's own interface is documented

`CLUSTER MEET`, `CLUSTER NODES`, `SENTINEL MONITOR`, `SENTINEL MASTER` and
`SLAVEOF` are documented in the shipped `README.md` and in the command table in
`src/redis.c`; the on-disk formats they feed are the `nodes.conf` written by
`clusterSaveConfig` and the rewritten `sentinel.conf` written by
`sentinelFlushConfig`. The annotated `redis.conf` and `sentinel.conf` in the
workspace root document every directive an operator can set.
