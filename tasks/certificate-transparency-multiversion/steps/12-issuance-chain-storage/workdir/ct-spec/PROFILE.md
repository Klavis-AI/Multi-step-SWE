# PROFILE — step 12 of 13: issuance chain storage (certificate-transparency-go v1.2.0)

## What this milestone adds over v1.1.4

The log learns to stop repeating itself, and it learns it **optionally**. This
is the one release in the series whose headline capability is off by default
and whose whole correctness claim is that turning it on for one log changes
nothing for any other.

**The chain moves out of the leaf, if you ask.** Until now a log entry's extra
data has held the entire chain between the leaf and a trusted root, once per
submission, however many submissions shared it. v1.2.0 adds a second
layout in which the leaf holds only a **hash** of that chain and the chain
itself lives in a store the front end owns. Reads look the hash up and
rehydrate, so the bytes a client reads are identical either way. The mode is
per log, it is named in the configuration, and its default is the layout you
already have.

**A storage interface and a cache interface.** `IssuanceChainStorage`, with a
lookup by key and an add; a `cache/` package with a selector type, an LRU
implementation and a no-op one; and a layer joining the two, which sits behind
the front end's leaf construction and rehydration. A store that comes back
nil is not a failure — it is how the front end is told that a log keeps its
chains where it always did, and two new fields on `InstanceOptions` carry the
cache selection into instance set-up.

**New configuration and new flags.** `ctfe_storage_connection_string` and
`extra_data_issuance_chain_storage_backend` join `LogConfig` at field numbers
20 and 21, with a nested enum whose zero value is the old behaviour;
`--cache_type`, `--cache_size` and `--cache_ttl` join `ct_server`, taking it
to 23 flags. Because the configuration is parsed as text proto, which rejects
a field it does not know, both fields are observable simply by naming them and
looking at the exit status. Choosing the new backend without a connection
string, or naming a driver that does not exist, stops the process from
starting.

**Keys are cross-checked.** A configured public key that is not the public
half of the configured private key is now a start-up failure, and so is a
configuration in which two logs share one private key.

**Two statuses move.** Backend quota exhaustion stops looking like an
authorisation failure and becomes **429**; a backend timeout stops looking
like the client timing out and becomes **504**.

**One removal:** the second log-list generation goes, leaving exactly one.
**One withdrawal:** `ct_hammer` loses `seed`. **One client-side guard:** the
upload path stops reading a redirect-converted POST as a successful
submission. A 301, 302 or 303 turns a POST into a GET, the GET comes back 200,
and until now that looked like an accepted submission; the client now treats it
as an error. "Treats it as an error" is an obligation about the whole call and
not only about its error return: nothing from the redirected reply is decoded
into the caller's response value, which is left exactly as the caller passed it
in (CLIENT-API.md §1.1). Redirects that preserve the method, 307 and 308, are
followed as before, and still parse. The server issues no redirects itself
— this guards against somebody else's.

## Reading route through ct-spec/

1. **CLIENT-API.md** §8.4 — the storage and cache surface; this is the
   milestone.
2. **CONFIG.md** §2.6 — the two fields, the enum, the validation, and the
   sentence that matters: the wire format does not change.
3. **CONFIG.md** §1 and §1.1 — the field numbers, and why an unknown field
   name is itself observable.
4. **CONFIG.md** §2.3 — the two key rules that arrive here.
5. **TLS-CODEC.md** §5.4 — the chain wrappers the hashed forms sit beside,
   and **CLIENT-API.md** §8.7 for the two leaf builders that produce them
   and the three leaf fields that do not change between the layouts.
6. **CLI.md** §2.1 — the three cache flags and the 23-flag set; §2.2 for the
   order in which start-up fails.
7. **HTTP-API.md** §6.1 — the two cells that move; §2 for the request wrapper
   they are returned through, which is also where the server's own "no
   redirects anywhere" rule is stated. Then **CLIENT-API.md** §1.1 for
   `jsonclient`, the upload transport the client-side guard lives in.
8. **CLIENT-API.md** §4.4 — the log-list removal.
9. **RELEASE-MAP.md** §12 — the milestone on one screen, and §14 for the
   absence to check.

## How you will know you are done

A log left on the default backend queues a leaf whose extra data is the whole
chain, byte for byte, with or without a cache configured; a log switched to
frontend storage queues a leaf whose extra data is a hash, with the same
Merkle leaf and the same leaf hash as the first; `get-entries` returns the
same `extra_data` for both; the cache factory builds an LRU or a no-op and
refuses a bad size, a bad TTL and an unknown type; a mismatched public key and
a shared private key both stop start-up; the CTFE backend without a connection
string stops start-up; `ResourceExhausted` comes back as 429; and the second
log-list package is gone.
