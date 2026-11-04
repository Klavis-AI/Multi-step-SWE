# Step 4 — the symmetric half, moved onto ciphers fetched by name

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, draws its secrets from the private
generator, creates a library context of its own in `_cpri__InitCryptoUnits`,
loads a provider into it by name, and fetches every digest it hashes with out
of that context.

One part of it did not come along. The engine's symmetric block cipher layer is
still written the way it was written against 1.0.2: `AES_set_encrypt_key()`
builds a key schedule into an `AES_KEY` on the caller's stack, `AES_encrypt()`
transforms one sixteen-byte block, and the chaining for CBC, CFB, CTR, ECB and
OFB is written out by hand around those two calls, a loop per entry point.

That API is deprecated at 3.0 (`OSSL_DEPRECATEDIN_3_0`), and deprecation
understates it: the release leaves it behind more completely than a deprecation
leaves anything. A key schedule built on the stack is not a provider's key, and
nothing a provider decides can reach it. The
library context the engine now owns has no say over `AES_encrypt` — not which
implementation runs, not whether the algorithm is allowed, not what a property
query would have selected. All of that stops at the digests.

At 3.0 a cipher is obtained the way a digest is: fetched by name from a library
context, reference counted, given back with `EVP_CIPHER_free`, and driven
through an `EVP_CIPHER_CTX`. `EVP_aes_128_cbc()` still compiles and still
returns something that encrypts correctly — but what it returns comes from
whatever the *process default* context holds, which is precisely the source
this tree stopped taking its digests from. A port that swaps the hand-written
loops for one of those constructors has written code that looks like this step
and is not it.

This machine has OpenSSL 3.0.17 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

Move the engine's symmetric half onto it: every cipher fetched by name from the
context the engine already made, held rather than taken per operation, every
operation's bytes handed to EVP, and `<openssl/aes.h>` out of the tree.

The bookkeeping is the easy half of that. The work is at the boundary. Six of
the eight symmetric entry points take `BYTE *iv` in *and out* — ECB, which
chains nothing, has no such parameter — and what those six leave behind is
where the next chained call starts; the TPM above chains them routinely. EVP
keeps a register of its own, and for every one of the four modes that has an IV
it is either not the same thing or not reachable in the same form. Section 2
states each of the four, because an IV that is one mode's convention instead of
another's is a wrong answer that builds, links, runs, round-trips against
itself, and is caught by nothing the tree does to itself.

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

**The ciphers come from a fetch, by name, against the engine's own context.**
Every cipher the engine encrypts or decrypts with is obtained by fetching it by
name from the library context `_cpri__InitCryptoUnits` makes. Not from the one
the process happens to share: a fetch that names no context gets its algorithm
from the default, which is where it came from before, so a fetch written
against `NULL` has changed the spelling and not the source.

The built-in constructors are gone from the sources the build compiles and from
what the built engine asks the loader for. Named exactly, so that there is
nothing to guess at: for each of `128`, `192` and `256`, the constructor
`EVP_aes_<size>_<mode>` for each of `ecb`, `cbc`, `cfb`, `cfb1`, `cfb8`,
`cfb128`, `ofb` and `ctr` — twenty-four names — and the two lookups that
predate fetching, `EVP_get_cipherbyname` and `EVP_get_cipherbynid`, which hand
back a built-in cipher out of libcrypto's own table. The point of a fetch is
that the algorithm comes from a named provider in a context the engine
controls; a lookup that still returns a record out of a static table has not
made that true. Asking the loader for one of these names at run time is still
asking for it: an engine that links what it calls has no use for `dlopen`,
`dlsym` or `dlvsym`, and none of the three appears in the sources the build
compiles. That holds for every name this step says is gone, not just these.

Gone from the sources means gone from their text. A call that is still written
out but switched off — parked in a branch the preprocessor never takes, or
behind a macro nothing defines — is code this step removes rather than code it
allows, and putting it out of the compiler's reach is not taking it out of the
tree. A comment is the exception in the other direction: prose that mentions a
name the engine no longer calls is a note about the migration, not a use of it.

And the other end of the same requirement, which is a separate claim: the
engine encrypts with nothing else. Every call that hands a cipher to an
`EVP_CIPHER_CTX` and starts using it is handed one this engine fetched. Fetching
ciphers at startup, holding them, freeing them at shutdown and then encrypting
with `EVP_aes_128_cbc()` anyway satisfies every sentence in the paragraph above
and fails this one. (Re-keying a context that already has its cipher — passing
`NULL` where the cipher goes — names nothing new and is neither here nor there.)

**What was fetched is held.** A fetched cipher is reference counted. The engine
fetches each cipher it needs once and holds it for as long as it needs it, and
gives it back when the engine shuts down — so no operation the engine performs
on a caller's behalf takes a cipher and gives it back around itself. There is
more than one reasonable place to do the fetching, and more than one is
accepted: the tree needs at most fifteen ciphers, five modes over three key
sizes, and building all fifteen eagerly at startup and filling a cache lazily on
first use are both fine. What is not accepted is a fetch per operation, whether
it gives the reference back afterwards or leaks it. How often the engine fetches
does not grow with how much symmetric work it does: a run that performs many
symmetric operations fetches no more than a run that performs few, because what
those operations use is already held. By the time the engine has shut down,
every reference it took has been given back and none is still held. None of
this is a claim a tree that does not answer those entry points can make: an
engine that performs no symmetric operations is holding nothing for them, and
does not satisfy this paragraph by omission.

**Every symmetric operation's bytes go through EVP.** This is the positive, and
it is a separate requirement from everything above because everything above can
be satisfied without doing any of this step's work. A tree that fetches fifteen
ciphers from its own context, holds them, frees them at shutdown and then
encrypts with the `AES_encrypt` loop it already had passes the paragraphs above
completely. So does a tree that deletes `<openssl/aes.h>` and writes two hundred
lines of its own AES in C — a ban is satisfied by deletion. What neither of them
does is hand the data to the library: for every symmetric operation the engine
performs, the plaintext or ciphertext is passed to EVP — `EVP_EncryptUpdate`,
`EVP_DecryptUpdate`, `EVP_CipherUpdate` or `EVP_Cipher`, whichever suits the
shape you chose — at least once. A port that feeds EVP a block at a time is
doing the same thing more often, which is fine; a port where an operation's
bytes never reach any of those four has done the work somewhere else.

**The low-level symmetric API is gone.** None of it appears in the sources the
build compiles or in what the built engine asks the loader for, and none of it
runs. Named exactly: `AES_set_encrypt_key`, `AES_set_decrypt_key`,
`AES_encrypt`, `AES_decrypt`, `AES_ecb_encrypt`, `AES_cbc_encrypt`,
`AES_cfb128_encrypt`, `AES_cfb1_encrypt`, `AES_cfb8_encrypt`,
`AES_ofb128_encrypt`, `AES_ctr128_encrypt`, `AES_ige_encrypt`,
`AES_bi_ige_encrypt`, `AES_wrap_key`, `AES_unwrap_key` and `AES_options` — and
`AES_KEY`. That last one is a type rather than a function, and it is on the list
for the same reason as the rest: it is the object every one of the eight entry
points puts on its stack, it is the thing the header exists to declare, and a
tree that still declares one has kept the API whatever it does with it. Not one
of those seventeen names is written anywhere in the sources this build
compiles.

**The shim goes with the block it serves.** The engine carries a second copy of
the same eight entry points for SM4, inside `#ifdef TPM_ALG_SM4`, and beside
them a shim that lets those compile without an SM4 implementation: `typedef
AES_KEY SM4_KEY;` and four `#define`s aliasing `SM4_set_encrypt_key`,
`SM4_set_decrypt_key`, `SM4_encrypt` and `SM4_decrypt` onto their AES
counterparts. The tree's own configuration sets `ALG_SM4` to `ALG_NO`, so none
of that is compiled, no symbol is imported on its account and nothing in it
runs — and every one of those names is still written down. The previous
paragraph is about the text, so the shim and the block it serves go together.
Migrating the AES bodies and leaving the SM4 branch standing is the one way to
finish this step and still have `AES_KEY` in the tree.

**The IV each entry point leaves behind is part of its answer.** Six of the
eight take `BYTE *iv` in and out, sixteen bytes, and the value left there is
what a chained continuation starts from. ECB is the other two: it has no `iv`
parameter, it chains nothing, and this interface does not pretend otherwise.
EVP has a register too, and for each of the four modes that has an IV it is
either not the same quantity or not reachable in the same form. What each entry
point must leave in `iv` when it returns, unchanged from what this tree does
today:

* **CBC** — the last block of ciphertext. On encrypt that is the last sixteen
  bytes written to `dOut`; on decrypt it is the last sixteen bytes of `dIn`.
  A caller may pass one buffer as both `dIn` and `dOut`, and this tree's own
  callers do.
* **CFB** — the last bytes of ciphertext, **zero filled to sixteen** when the
  data ran short. When the length is a multiple of the block size this is the
  last full ciphertext block; when it is not, it is the final partial block's
  ciphertext bytes followed by zeros to sixteen. EVP's CFB register keeps the
  unused key stream in those trailing positions instead. Encrypt and decrypt
  both leave the ciphertext there — for decrypt that is the input, not the
  output.
* **CTR** — the counter, advanced once per block *begun*: a call of twenty
  bytes advances it twice. It is a 128-bit big-endian integer over the whole
  sixteen bytes, incremented from the last byte with carry, and it wraps.
* **OFB** — the key stream register, which is neither the plaintext nor the
  ciphertext and cannot be recovered from them when the final block is short.
  This is the one that is not bookkeeping: after the operation, `iv` holds the
  key stream block that the last byte of output was XORed against — the whole
  block, including the part of it no data consumed.

A call with nothing to do is still a call: a zero-length input returns success
and leaves `iv` exactly as it found it, before anything is keyed. And a key size
the engine cannot serve remains the fatal error it is today, not a return code.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are. In particular the eight
symmetric entry points keep their names, their parameters in their order, and
their results: `CRYPT_SUCCESS`, and `CRYPT_PARAMETER` where a length that is not
a multiple of the block size returns it today. `_cpri__GetSymmetricBlockSize`
still answers for the algorithms it answers for now.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served.

**Behaviour is unchanged.** For every input, the engine and the simulator built
from this tree produce exactly what they produce today: the same ciphertext for
the same key, IV and plaintext, the same outgoing IV, the same digests, the same
signatures, the same public keys for the same seeds, the same response codes,
the same error and failure-mode behaviour on bad input. A change in an answer is
a defect in the port, whatever else improved. Where an answer is random, what has
to stay the same is its shape — how much output the operation produces, and that
separate draws remain independent of one another — not the bytes.

One thing a fatal error carries is not part of that answer. `TpmFail` is handed
the source position that raised the failure — a function name and a `__LINE__`
— and any edit above a failure site moves the line, so every port moves some of
them. What has to match is the fatal code the engine chose, and that the TPM is
in failure mode at all. The source position is not part of it, including where a
failure-mode `TPM2_GetTestResult` hands it back: it records where the source
was, not what the engine did.

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
through it; ECDSA signed and verified by libcrypto's own ECDSA
API — `ECDSA_do_sign` and `ECDSA_do_verify` on a key object, the signature's
components read back through the `ECDSA_SIG` accessors, with no modular
inversion or point multiplication of the engine's own left in either; a library
context of the engine's own, created at startup and freed at shutdown,
with a provider loaded into it by name. A step that moves forward by undoing one
of those has not moved forward.

Three of them are where this step lands, so they are worth spelling out:

* **The digests are still fetched by name from the engine's own context, and
  still held.** This is the requirement this step is most likely to break by
  accident rather than by choice: making room for a cipher table means touching
  the engine's startup, and a digest fetch that moves out of
  `_cpri__InitCryptoUnits`' reach, or a digest that starts being fetched per
  hash because the caching got reorganised, fails a requirement the tree
  already holds. It still holds here, and so does everything that came with
  it. None of `EVP_sha1`, `EVP_sha224`, `EVP_sha256`, `EVP_sha384`,
  `EVP_sha512`, `EVP_sm3`, `EVP_get_digestbyname` or `EVP_get_digestbynid`
  comes back. Neither does the low-level digest API: for each of `SHA1`,
  `SHA224`, `SHA256`, `SHA384` and `SHA512`, neither the bare name nor that
  name followed by `_Init`, `_Update`, `_Final` or `_Transform`. Neither do the
  accessors that used to reach a digest's private state —
  `EVP_MD_CTX_md`, `EVP_MD_CTX_get0_md_data` and
  `EVP_MD_meth_get_app_datasize`, which still exist at 3.0 and for a fetched
  digest return nothing useful. Gone means gone from the text of the sources
  the build compiles, gone from what the built engine imports, and not asked of
  the loader — the `dlopen`/`dlsym`/`dlvsym` sentence above is about every name
  this instruction says is gone, the digest ones included.
* **A hash sequence state is still a value.** The layers above the engine
  relocate, duplicate, write out and abandon the structure it lives in, with
  `memcpy` and `memset`, without asking, and every copy still has to work.
* **The library context is still created and still freed.** One context, made by
  `_cpri__InitCryptoUnits` and released by `_cpri__StopCryptoUnits`, with the
  provider loaded into it and unloaded from it. Whatever this step fetches, it
  fetches from that one, and it gives back what it took before the context that
  issued it goes away.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
