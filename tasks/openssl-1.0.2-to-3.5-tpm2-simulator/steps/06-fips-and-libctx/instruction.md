# Step 6 — the FIPS module, loaded into the engine's own context and named

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, draws its secrets from the private
generator, creates a library context of its own in `_cpri__InitCryptoUnits`,
loads a provider into it by name, and fetches every digest, every cipher and
every public key it operates on out of that context.

Every one of those fetches passes `NULL` as its property query. Until now that
has been the right answer, because there has been exactly one provider in the
context and nothing for a query to choose between.

This step puts a second one there.

This machine has OpenSSL 3.1.8 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available. It
ships a FIPS module — `fips.so` in the module directory — and, beside it, a
configuration file that describes that module to the library. An operator who
has to attest to the module's integrity needs the engine to load it, run its
self tests, and hold it for as long as the engine runs.

Loading it is not a matter of linking. The module is handed its own integrity
MAC as configuration data, and a library context that has never loaded a
configuration cannot load the provider at all: `OSSL_PROVIDER_load` returns
`NULL` with `SELF_TEST_post: missing config data` however correct the file on
disk is. So the configuration has to go into the context the engine made, and
then the module comes out of that same context.

**And then the engine has to go on producing exactly the answers it produces
today**, which is where the property query stops being a formality. With two
providers answering to one context, what an unqualified fetch resolves to is
decided by the order they were loaded in — the first explicit
`OSSL_PROVIDER_load` wins, and where there was no explicit load at all, the
configuration's own activation wins. Nothing in the source records that
decision, and it is not a small one: this TPM implements RSA at 1024 and 2048
bits and exercises both, and it builds all three of its EC curves from fully
explicit parameters. The module refuses a 1024-bit public-key encryption
outright, and refuses explicit curve parameters as a class. Section 2 says what
happens to each of them.

So the step is three things:

* the configuration loaded into the engine's own library context;
* the module loaded from that context, self-tested, held for the life of the
  engine, and given back when the engine stops — beside the provider the engine
  already had, not instead of it;
* every fetch and every key-operation context in the engine saying which
  implementation it wants, instead of inheriting whichever provider happened to
  load first.

**This step does not make the engine compute in FIPS mode.** It cannot: a TPM
that could only do what the module does would not be this TPM. The module is
present, loadable, self-tested and released; the engine's answers are unchanged.

Two things about the tree as it stands are worth knowing in advance, because
they look like failures of the port and are not. Both are in the platform and
simulator layers, in code that calls no OpenSSL, and the tree does both of them
before anything in this step is done to it.

* The simulator does not always get as far as listening. A launch aborts during
  manufacture a few times in forty, and the same command line retried succeeds.
* `Simulator <port>` binds its port without asking for the address to be
  reusable, so a port that has carried a connection cannot be bound again for
  about a minute after the simulator that served it goes away: a second launch
  on that port inside that minute prints `Bind error.` and exits, while nothing
  is listening on it. Give each launch a port that has not been used yet — the
  supply is not short — or wait the minute out.

## 2. Requirements

**Build.** The tree still configures and builds out of tree with its own build
system, unchanged in how it is driven:

```
cmake -S /app/cmake -B <build directory> -DSPEC_VERSION=116
cmake --build <build directory>
```

Those two commands and nothing on either of them that is not written there. The
build still produces all four of `platform/libplatform.so`,
`OsslCryptoEngine/libCryptoEngine.so`, `tpm/libTPM.so` and `Simulator` in the
build directory, and configuring that same directory again with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added still writes a
`compile_commands.json` describing what this tree compiles and with what flags.

**OpenSSL comes from the system, and it is this one.** Every object the build
produces resolves its OpenSSL symbols through the dynamic loader at run time and
defines none of them itself, and the libcrypto the loader resolves is the
installed 3.1.8. No copy of a library travels with the tree.

**The configuration is loaded into the engine's own library context.** The
engine's startup entry point — `_cpri__InitCryptoUnits`, the same one that makes
the context and loads the provider today — reaches `OSSL_LIB_CTX_load_config`,
and it is called on the context `OSSL_LIB_CTX_new` gave this engine. Directly or
through a helper it reaches; what matters is that startup is where it happens.
This image ships a configuration file that describes the module to the library,
in the place this release keeps such things; finding it is ordinary work with
the tools already installed. Which configuration file the engine loads is the
port's business and nothing here pins it, so long as the engine finds it on
this machine as it is set up — a file the engine reads from the tree has to be
in `/app`, which is the result handed over; what does not vary is which
*context* it goes into.

Two spellings are the mistake this requirement exists to rule out, and both of them
compile, return success, and make the module loadable:

* `CONF_modules_load_file(path, NULL, 0)` configures the **process**. It is the
  pre-3.0 call and it knows nothing about library contexts.
* `OSSL_LIB_CTX_load_config(NULL, path)` is the same mistake written in this
  step's own vocabulary: `NULL` there means the process default context, which
  is precisely the context this engine was taken off when it was given one of
  its own.

A configuration that went somewhere else is not this context's configuration,
and the context the engine fetches from is the one it made.

The load has to succeed, and the call reports whether it did: it succeeds for
the file this image ships and fails for a path that is missing, unreadable or
malformed. A port that drops the report proceeds to a provider load that will
fail for a reason it has already thrown away.

**The FIPS module is loaded, by name, into that same context.**
`OSSL_PROVIDER_load` is called with `"fips"` and returns a handle the engine
keeps. The handle is the point rather than the name: the self test below takes
an `OSSL_PROVIDER *`, and a module nobody holds cannot be self-tested and cannot
be given back. Activating the module from the configuration file alone leaves no
handle and does not satisfy this.

**The provider the engine already had stays.** This step *adds* a provider; it
does not swap one. The `"default"` provider this context already holds is still
loaded into it. A port that replaced the one with the other would satisfy every
sentence above about the module and would lose every 1024-bit RSA encryption
and every EC key this engine builds.

**Nothing the engine fetched before this context existed is still in use after
it.** This engine keeps two tables of algorithm objects that are
fetched once and held — the digests and the ciphers — and the guard that makes
"once" mean once keeps whatever is already in them. Those tables are filled
before `_cpri__InitCryptoUnits` ever runs: the simulator manufactures the TPM at
start-up, manufacturing hashes, and the startup entry point the TPM calls for
that is not the one that makes the context. So by the time this step's context
exists, both tables can already be full of objects fetched with a null
context — which is the process default, which is the context this engine was
taken off when it was given a context of its own.

Nothing in this tree notices. A digest and a cipher behave the same whichever
context they came out of, the answers are identical, and the source still names
the engine's own context at every fetch. That is the point: **this requirement is
about the objects the engine is actually holding, not about the argument its
fetches are written with.** A tree that satisfies every other sentence in this
section and leaves the tables as it found them has a context it does not fetch
from, and the first thing in the library that compares two objects' providers
will find they do not match.

Where the drop goes is the port's business; that it happens before the tables
are read again is not. The engine already has the two cleanup entry points this
needs — shutdown calls them — and what they give back is exactly what a new
context makes stale.

**The module is self-tested at startup, and the answer is acted on.**
`_cpri__InitCryptoUnits` reaches `OSSL_PROVIDER_self_test`, it is called on the
FIPS handle the engine loaded, and **what it returns decides whether the engine
starts**. `OSSL_PROVIDER_load` returning non-`NULL` says the module was found
and its MAC checked at load; it does not say the module's algorithm self tests
passed, and those are what the attestation is about.

So the result is a value this engine has to use, not a call it has to make. An
engine that makes the call and discards what it says is correct for exactly as
long as the module is intact, and starts anyway on a module that is not. If
the self test does not report success then startup does not complete: it takes
the failure path this engine already has for a startup that cannot proceed, the
same one the provider load beside it takes — `FAIL()`, or a return from
`_cpri__InitCryptoUnits` other than `CRYPT_SUCCESS`, which the TPM above the
engine turns into the same failure mode — and no cryptography runs on a module
the engine has just been told it cannot trust.

**The module stays loaded for the whole run, and is given back at the end.**
Not unloaded on the line after the self test, not unloaded anywhere before
shutdown: a FIPS provider held for the length of one function is not a module
the engine is running under. `_cpri__StopCryptoUnits` reaches
`OSSL_PROVIDER_unload`, and **every** provider the engine loaded is given back
there — the module and the default provider both, one unload each. A leaked FIPS
provider is not a leaked allocation: the module holds its self-test state and
its DRBG chain, and a shutdown that does not release it leaves a TPM that cannot
be restarted in the same process, which is what `_cpri__StopCryptoUnits` exists
to make possible.

With one provider in the context, "a provider is loaded at startup" and "a
provider is unloaded at shutdown" were the same sentence. With two they are not,
and the count is the requirement: as many providers given back at shutdown as
the engine is holding.

**Every fetch and every key-operation context names the implementation it
wants.** Every one of these calls the engine makes while it runs is given a
property query, and none of them is given `NULL` or `""`:

| call | argument |
| --- | --- |
| `EVP_MD_fetch(ctx, name, propq)` | `propq` |
| `EVP_CIPHER_fetch(ctx, name, propq)` | `propq` |
| `EVP_PKEY_CTX_new_from_name(ctx, name, propq)` | `propq` |
| `EVP_PKEY_CTX_new_from_pkey(ctx, pkey, propq)` | `propq` |

There are as many queries as there are calls: naming the query at three of the
four and leaving the rest is not this. Neither is moving a call to a spelling
that has nowhere to put one — `EVP_PKEY_CTX_new` builds a key-operation context
and takes no property query, so a key-operation context built through it has
named nothing. The requirement is about every fetch and every key-operation
context the engine makes, not about four function names: a fetch this port
introduces under another spelling is under it too.

**What the query says is the port's decision, and this step does not make it.**
Any query that keeps this engine's answers is a right answer here, and which
implementation a deployment wants is the deployment's to choose. What the query
may not do is leave the choice to somebody else. A query that constrains
nothing — no query, an empty one, or one whose every clause is optional and
matches nothing — resolves to whichever implementation happens to rank first in
the context, and that ranking is a property of the two adjacent lines that
loaded the providers rather than of anything a reader of the fetch can see. A
port that loads `"default"` before `"fips"` and leaves its queries unconstrained
computes the right answers today and will compute different ones the day
somebody swaps those two lines, or drops the explicit load and lets the
configuration bring the module up on its own. Naming the implementation at the
fetch is the migration; getting the right answer by ordering is not.

Both of the module-preferring answers are wrong on this tree, and they are wrong
differently. A mandatory `"fips=yes"` sends 1024-bit RSA encryption and every
explicit-parameter EC key to an implementation that refuses them. The
non-mandatory `"?fips=yes"` looks like the careful hedge and is not: it falls
back to the default provider only where the module does not advertise the
algorithm **at all**, and the module advertises RSA and EC. It then refuses
1024-bit RSA encryption at `EVP_PKEY_encrypt_init` ("invalid key length") and
refuses `OSSL_PKEY_EC_ENCODING_EXPLICIT` as a class at `EVP_PKEY_fromdata`,
before looking at which curve it is — so NIST-P256 is refused exactly as hard
as `BN_P256`.

**Behaviour is unchanged.** For every input, the engine and the simulator built
from this tree produce exactly what they produce today: the same ciphertext for
the same key, IV and plaintext, the same outgoing IV, the same digests, the same
signatures, the same public keys for the same seeds, the same response codes,
the same error and failure-mode behaviour on bad input. A change in an answer is
a defect in the port, whatever else improved. Where an answer is random, what
has to stay the same is its shape — how much output the operation produces, and
that separate draws remain independent of one another — not the bytes. An ECDSA
signature and an OAEP or RSAES ciphertext are in that category: they are
randomised, and what has to hold is that they verify and decrypt.

One thing a fatal error carries is not part of that answer. `TpmFail` is handed
the source position that raised the failure — a function name and a `__LINE__` —
and any edit above a failure site moves the line, so every port moves some of
them. What has to match is the fatal code the engine chose, and that the TPM is
in failure mode at all. The source position is not part of it, including where a
failure-mode `TPM2_GetTestResult` hands it back: it records where the source
was, not what the engine did.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are. Whatever this step adds for the
engine's own use is the engine's own; `CryptoEngine.h` is what the engine
publishes, and nothing this step asks for needs a new entry in it.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served.

**Everything this tree already establishes stays established.** No OpenSSL
header and no OpenSSL static archive anywhere under `/app`; no expression
reading or writing a field of a structure or union OpenSSL declares; nothing
holding such a structure by value where the installed OpenSSL no longer
publishes its definition; every `EVP_MD_CTX` obtained from libcrypto's allocator
and given back to it; libcrypto initialised explicitly by the engine's own
startup entry point; secret draws from the private generator and public draws
from the public one, kept apart; fixed-width big-endian conversion done by the library's one call, with the narrower
conversion it replaced, `BN_bn2bin`, neither called nor imported anywhere; secret buffers erased with the library's eraser; a hash
sequence state still a value the layers above can relocate, duplicate and
abandon with `memcpy` and `memset`; one library context of the engine's own,
made by `_cpri__InitCryptoUnits` and released by `_cpri__StopCryptoUnits`;
every digest and every cipher fetched by name from that context, held rather
than re-fetched per operation and given back when the engine stops, with no digest context's private state read and
no digest computed below EVP; every asymmetric key built with
`EVP_PKEY_fromdata` from an `OSSL_PARAM` list, in that same context, and freed
by the call that built it; the public halves of RSA and ECDSA driven through
`EVP_PKEY_encrypt`, `EVP_PKEY_verify` and `EVP_PKEY_sign`, with the hand-rolled
public-side padding encoders gone and the private-side ones still compiled and
still reached from the entry points that own them; the private halves still the engine's own constant-time exponentiation through
`RSADP`, on a path of decryption's own — the engine's published
modular-exponentiation entry point does not take the constant-time path
itself, and decryption no longer goes through it —; and none of
the deprecated `EC_KEY`, `ECDSA_*` or low-level `RSA_*` and `SHA*`/`AES_*`
families anywhere in the sources, the imports or the run. A step that moves
forward by undoing one of those has not moved forward.

Three of them are where this step lands, so they are worth spelling out:

* **The digests and the ciphers are still fetched once and held.** This step
  edits the two lines that fetch them. It changes an argument; it does not turn
  a startup fetch into a per-operation one, and the count of fetches still does
  not grow with the work.
* **The keys are still built in the engine's own context.**
  `EVP_PKEY_CTX_new_from_name` still names the engine's own context as its library
  context — that argument and the property query are different arguments of the
  same call, and only the second one is what this step is about.
  `EVP_PKEY_CTX_new_from_pkey`'s library-context argument is not what this
  step is about; its property query is, and is what this step asks for.
  A key-operation context is still made for the operation that needs it and
  freed before the call that made it returns. Unlike a fetched `EVP_MD`, which
  is fetched once at startup and held for the life of the engine, an
  `EVP_PKEY_CTX` is not something this engine keeps: the only ones alive at any
  moment are the ones whose operation is in flight.
* **`_cpri__StopCryptoUnits` still frees the context it was already freeing.**
  Providers are given back before the context they live in, not instead of it.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
