# Step 3 — the library context, the provider, and a digest state that can no longer be read

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches OpenSSL's
types only through accessors, draws its secrets from 1.1.1's private generator
and signs ECDSA with the library's own. In one respect it is still written the
way a program was written in 1998: it gets its algorithms from libcrypto's
built-in table — `EVP_sha256()` returns a pointer to a static record the library
owns and nobody frees — and when the TPM asks it to hand back a hash sequence it
reads the private bytes behind an `EVP_MD_CTX` and copies them into the caller's
storage.

OpenSSL 3.0 is where both of those stop. Algorithms come from *providers*: the
way to ask for one is to fetch it by name from a *library context*, which is an
object with a lifetime that the caller creates, holds and frees, and the fetched
algorithm is reference counted and has to be given back. And the state behind an
`EVP_MD_CTX` moved into the provider. For a provided SHA-256,
`EVP_MD_CTX_get0_md_data()` is `NULL` and `EVP_MD_meth_get_app_datasize()` is
`0`, and no public 3.0 API names the running digest state. Confirm that on the
installed headers before keeping a marshal of `EVP_MD_CTX` bytes.

That second half is the work. The engine's hash sequence state is a field inside
a structure the TPM above it treats as a plain value: it relocates that
structure with `memcpy`, duplicates it, writes it out as a context blob and
abandons it with `memset`, and it does all of that without telling the engine. A
sequence state that is a live pointer into the engine's own storage is therefore
not a value, and no amount of reference counting makes it one: after a
relocation either copy may be updated, completed or discarded, in any order, and
a copy may be completed after the original already was.

This machine has OpenSSL 3.0.17 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

Move the engine onto it: a library context of its own, a provider loaded into it
by name, its digests fetched from it, and a representation for a hash sequence
state that is still a value under `memcpy` when the bytes of the digest cannot
be obtained.

Two things about the tree as it stands are worth knowing in advance, because
they look like failures and are not. Both are in the platform and simulator
layers, in code that calls no OpenSSL, and the tree does both of them before
anything in this step is done to it.

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
installed 3.0.17. No copy of a library travels with the tree.

**The engine owns a library context.** `_cpri__InitCryptoUnits`, the engine's
own startup entry point, creates a library context of the engine's own rather
than working in the one the process shares. `_cpri__StopCryptoUnits`, the
engine's own shutdown entry point, frees it. Both halves are required: a context
created and never released is a leak, and a release with no context to release
is nothing.

**The provider is loaded by name.** The context the engine made gets a provider
loaded into it, by name, at startup — not activated behind the engine's back on
first use — and that provider is unloaded at shutdown. A provider is loaded
*into* a context, so this and the context are one decision: a port with one and
not the other has not got a working fetch.

**The digests come from the provider.** Every digest the engine hashes with is
obtained by fetching it by name from the library context the engine made. Not
from the one the process happens to share: a fetch that names no context gets
its algorithm from the default, which is where it came from before, so a
context created and then not fetched against is a context that does nothing.

The built-in getters are gone from the sources the build compiles and from what
the built engine asks the loader for: none of `EVP_sha1`, `EVP_sha224`, `EVP_sha256`,
`EVP_sha384`, `EVP_sha512`, `EVP_sm3`, `EVP_get_digestbyname` or
`EVP_get_digestbynid` remains. The point of a fetch is that the algorithm comes
from a named provider in a context the engine controls; a lookup that still
returns a record out of libcrypto's static table has changed the spelling and
not the source. Asking the loader for one of those names at run time is still
asking for it: an engine that links what it calls has no use for `dlopen`,
`dlsym` or `dlvsym`, and none of the three appears in the sources the build
compiles. That holds for every name this step says is gone, not just these.

Gone from the sources means gone from their text. A call that is still written
out but switched off — parked in a branch the preprocessor never takes, or
behind a macro nothing defines — is code this step removes rather than code it
allows, and putting it out of the compiler's reach is not taking it out of the
tree. A comment is the exception in the other direction: prose that mentions a
name the engine no longer calls is a note about the migration, not a use of it.

**What was fetched is held.** A fetched digest is reference counted. The engine
fetches each digest it needs once and holds it for as long as it needs it, and
gives it back when the engine shuts down — so no operation the engine performs
on a caller's behalf takes a digest and gives it back around itself. There is
more than one reasonable place to do the fetching, and any of them will do; a
fetch per operation is not one of them, whether it gives the reference back
afterwards or leaks it. How often the engine fetches does not grow with how
much hashing it does: the public hash entry points this step is about can be
driven as hard as you like and the engine still fetches what it needs the once
— and they keep answering while it does, because an engine whose hash entry
points have stopped answering does not have this property at all.

**The digest's private state is neither read nor reached around.** Two things
follow from the state having moved into the provider, and both are required:

* The accessors that used to reach it are not used. Neither the sources the
  build compiles nor the built engine's imports contain `EVP_MD_CTX_md`,
  `EVP_MD_CTX_get0_md_data` or `EVP_MD_meth_get_app_datasize`. They still exist
  at 3.0 — two of them deprecated, none removed — and for a fetched digest they return
  nothing useful; a port that calls them anyway is a port that went looking.
* The low-level digest API is not the way around it. `SHA256_CTX` and its
  relatives are still published structures with published layouts, and an
  engine that abandons EVP for `SHA1_Init`/`SHA256_Update`/`SHA512_Final` and
  the rest of that API gets a `memcpy`-able digest state back and computes every
  correct answer without doing this step at all. None of that API appears
  anywhere in the sources the build compiles or in what the built engine asks
  the loader for. Named exactly, so that there is nothing to guess at: for each
  of `SHA1`, `SHA224`, `SHA256`, `SHA384` and `SHA512`, neither the bare name
  — `SHA256(d, n, md)` is a one-shot digest with no context to go with it — nor
  that name followed by `_Init`, `_Update`, `_Final` or `_Transform`. The
  digests come from the provider.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are. In particular the engine's hash
entry points keep their signatures and `CPRI_HASH_STATE` stays the type they
take, whatever the engine decides to put inside it.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served.

**Behaviour is unchanged.** For every input, the engine and the simulator built
from this tree produce exactly what they produce today: the same digests, the
same signatures, the same public keys for the same seeds, the same response
codes, the same error and failure-mode behaviour on bad input. A change in an
answer is a defect in the port, whatever else improved. Where an answer is
random, what has to stay the same is its shape — how much output the operation
produces, and that separate draws remain independent of one another — not the
bytes.

One thing a fatal error carries is not part of that answer. `TpmFail` is handed
the source position that raised the failure — a function name and a `__LINE__`
— and any edit above a failure site moves the line, so every port moves some of
them. What has to match is the fatal code the engine chose, and that the TPM is
in failure mode at all. The source position is not part of it, including where a
failure-mode `TPM2_GetTestResult` hands it back: it records where the source
was, not what the engine did.

One consequence of that is this step's whole difficulty, and it is worth
stating outright:

* **A hash sequence state is a value, and stays a value.** The layers above the
  engine relocate, duplicate, write out and abandon the structure a sequence
  state lives in, with `memcpy` and `memset`, without asking. After a
  relocation, either copy may be updated, completed or discarded, in any order,
  and a copy may be completed after the original already was. Two copies of a
  state are two sequences that go their own way from the moment the copy was
  taken. That is what the tree does today and what it has to keep doing.

What that state may no longer be is also worth stating, because it is the one
thing the release takes away rather than renames:

* **The sequence state can no longer be a copy of libcrypto's.** Today the
  engine marshals the private bytes behind an `EVP_MD_CTX` into the sequence
  object, and that is why the object survives being moved: the bytes are the
  whole of the state and they travel with it. At 3.0 there are no such bytes to
  read. Whatever the sequence object carries instead, the properties above are
  unchanged — the structure is still moved, duplicated and abandoned by layers
  that do not ask, and every copy still has to work.

**Everything this tree already establishes stays established.** No OpenSSL header
and no OpenSSL static archive anywhere under `/app`; no expression reading or
writing a field of a structure or union OpenSSL declares; nothing holding such a
structure by value where the installed OpenSSL no longer publishes its
definition; every `EVP_MD_CTX` obtained from libcrypto's allocator and given back
to it; libcrypto initialised explicitly by the engine's own startup entry point;
secret draws from the private generator and public draws from the public one,
kept apart; fixed-width big-endian conversion done by the library's one call, with the narrower
conversion it replaced, `BN_bn2bin`, neither called nor imported anywhere;
secret buffers erased with the library's eraser; the private-key exponentiation told its exponent is secret, on a path of
decryption's own — the engine's published modular-exponentiation entry point
does not take the constant-time path itself, and decryption no longer goes
through it; ECDSA signed and verified by libcrypto's own
ECDSA API — `ECDSA_do_sign` and `ECDSA_do_verify` on a key object, the
signature's components read back through the `ECDSA_SIG` accessors, with no
modular inversion or point multiplication of the engine's own left in either.
A step that moves forward by undoing one of those has not moved forward.

Two of them are where this step lands, so they are worth spelling out:

* The library context is an **addition** to the explicit initialisation the
  tree already does, not a replacement for it. `_cpri__InitCryptoUnits` still
  takes libcrypto up itself rather than leaving it to first use, and it now also
  makes the context. Dropping the one because the other arrived is a
  regression.
* An `EVP_MD_CTX` still comes from libcrypto's allocator and still goes back to
  it. Only the state *behind* the context moved into the provider; where the
  context itself comes from did not change.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
