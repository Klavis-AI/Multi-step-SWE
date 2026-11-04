# Step 2 — the private pool, the library's ECDSA, and OpenSSL 1.1.1

## 1. What this step is

`/app` is the TPM 2.0 reference simulator, with its crypto engine
(`src/OsslCryptoEngine/`) already building against an installed OpenSSL through
the accessors 1.1.0 published. In every other respect the engine is still
written the way it was written in 2014: one random pool for everything, a
fixed-width big-endian conversion open-coded five times over a narrower one,
signature nonces left lying in the frame that drew them, a private-key
exponentiation the library is never told is private, and its own ECDSA —
`k⁻¹(z + rd)` on the way out, `u₁G + u₂Q` on the way back — assembled out of
BIGNUM and EC_POINT primitives.

1.1.1 is where the first of those stops being a matter of style: it splits the
random generator in two, a public one and a private one, so that "this draw
becomes a key" is something the code can say rather than something a comment
claims. The other four need no new release at all. The library has published
the fixed-width conversion the engine keeps rewriting since 1.1.0, and an
eraser a compiler is not entitled to delete, a way to tell an exponentiation
that its exponent is a secret, and standard ECDSA with the signature object
around it for longer than that — the whole time this engine has been
hand-rolling them.

This machine has OpenSSL 1.1.1w installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

Move the engine onto it, and take those five pieces of 2014 out.

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
installed 1.1.1w. No copy of a library travels with the tree.

**Secret draws come from the private pool.** 1.1.1's generator has two halves.
Every draw the engine makes comes from one of them, chosen by what the bytes are
for:

* a value that becomes key material, or that protects it — a private key, a
  signature nonce, an ephemeral scalar — is drawn from the private half;
* a value that is not itself secret — one that travels inside the result the
  operation hands back, or one whose use the engine cannot see — is drawn from
  the public half. The engine's own published random-bytes entry point is one of
  these: a caller asked it for bytes and it does not know what the caller will do
  with them. So are the bytes that randomise an encryption or a signature
  encoding, which leave the TPM inside the ciphertext or the signature, as they
  do in libcrypto's own padding code.

A draw site takes from one half or the other, never from both, and which half it
takes from is settled by the code that makes the draw rather than by a flag some
single generator reads at run time: the point of there being two is that reading
the call says whether the bytes are secret.

Nothing in the engine's API changes for this, and no answer it computes changes
either: which bytes a draw returns is not one of the answers this tree has to
reproduce, only how many of them there are and that separate draws stay
independent of one another.

**Fixed-width conversion is the library's.** The engine turns BIGNUMs into
fixed-width big-endian byte strings in five places, each of them written out by
hand as a zero-fill followed by a narrower conversion into the right-hand end of
the buffer. The library publishes that operation as one call. Use it.
Neither the hand-written form nor the narrower conversion it is built on remains
anywhere in the sources the build compiles, and the built engine no longer asks
the loader for the narrower one.

**Secret bytes are erased with the library's eraser.** A signature scheme that
draws its per-signature nonce into a buffer of its own erases that buffer before
it returns. The nonce is as good as the private key — whoever holds it and the
signature it produced has the key — and the storage it sat in goes straight on to
whatever runs next. This applies to every scheme in the engine that still keeps a
nonce of its own once this step is done; more than one does, and they do not
resemble each other, so finding one is not finding them. A `memset` is not what
erases it, either: a compiler is entitled to delete a store to a buffer that
nothing reads afterwards, and in each of these nothing does. Use the function
libcrypto publishes for erasing a secret, which is defined not to be deleted.

**The private-key exponentiation is constant-time in the exponent.** RSA
decryption raises the ciphertext to the private exponent. Tell the library that
exponent is secret, so that the exponentiation takes the path whose running time
does not depend on it. The public-key operation is not changed, and neither is
the engine's published modular-exponentiation entry point: both take an exponent
as bytes from a caller and cannot know which kind they have. That entry point
therefore cannot be where decryption gets its answer any more, and decryption
stops going through it.

**Standard ECDSA is the library's.** The engine signs and verifies ECDSA with
point arithmetic it does itself. Both directions become calls into libcrypto's
own ECDSA API — `ECDSA_do_sign` and `ECDSA_do_verify` — on a key object built
from the same curve and the same key. The `ECDSA_SIG` signature object
libcrypto returns is opaque — its components are read back out through the
library's `ECDSA_SIG` accessors, not out of its fields. The modular inversion
and the point multiplication those two routines did for themselves are not work
either of them does any more. ECDSA is the scheme libcrypto's ECDSA API signs
and verifies. The engine's other elliptic-curve schemes stay as they are,
including the ones the installed library now offers an entry point of its own
for.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served.

**Behaviour is unchanged.** For every input, the engine and the simulator built
from this tree produce exactly what they produce today: the same digests, the
same public keys for the same seeds, the same response codes, the same error and
failure-mode behaviour on bad input. A change in an answer is a defect in the
port, whatever else improved. Where an answer is random, what has to stay the
same is its shape — how much output the operation produces, and that separate
draws remain independent of one another — not the bytes.

One thing a fatal error carries is not part of that answer. `TpmFail` is handed
the source position that raised the failure — a function name and a `__LINE__`
— and any edit above a failure site moves the line, so every port moves some of
them. What has to match is the fatal code the engine chose, and that the TPM is
in failure mode at all. The source position is not part of it, including where a
failure-mode `TPM2_GetTestResult` hands it back: it records where the source
was, not what the engine did.

One consequence of that is worth stating outright, because it is where this
step's two halves meet:

* **Handing the work to the library is not the same as handing the library the
  library's inputs.** libcrypto's ECDSA computes from exactly the bytes it is
  given. Everything this tree does to a digest, a length or a key before it
  reaches the arithmetic is part of this tree's answer, including the parts that
  are not what a textbook would do, and it has to keep happening. Read what the
  existing code does with its arguments before replacing what it does with them.

**Everything this tree already establishes stays established.** No OpenSSL header and no
OpenSSL static archive anywhere under `/app`; no expression reading or writing a
field of a structure or union OpenSSL declares; nothing holding such a structure
by value where the installed OpenSSL no longer publishes its definition; every
digest context obtained from libcrypto's allocator and given
back to it; libcrypto initialised explicitly by the engine's own startup entry
point. A step that moves forward by undoing one of those has not moved
forward.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
