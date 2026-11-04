# Step 7 — the engine's options travel as parameter lists, not as setter calls

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, draws its secrets from the private
generator, makes a library context of its own, loads the default provider and
the FIPS module into it, self-tests the module, and fetches every digest, every
cipher and every public key it operates on out of that context with a property
query that names the implementation it wants.

What none of that touched is how an operation is told **what to do** once it
exists. Two places in this engine still answer that the way 1.0.2 did: one
call per setting, each one a function whose name is the setting.

* `EccCurveInit` builds each of the TPM's curves in three calls — make a group
  from `p`, `a` and `b`; make a point and put the generator's coordinates in
  it; fit that point to the group as its generator, with an order and a
  cofactor. Three calls and a temporary point to say one thing: here is a
  curve. The three-call form also has nowhere to put a library context or a
  property query, which is why the rule that every fetch names the context it
  comes from stopped at its door.

* `_cpri__EncryptRSA` and `_cpri__ValidateSignatureRSA` make nine
  `EVP_PKEY_CTX_set_rsa_*` calls between them — a padding mode, an OAEP
  digest, an MGF1 digest, an OAEP label, a signature digest, a PSS salt length.
  On 3.x every one of those is a thin wrapper that packs its one argument into
  a one-element parameter list and hands it to the provider.

This machine has OpenSSL 3.2.5 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

This step replaces both of those with the 3.x form: the operation is handed its
parameters as a list, once, and the list carries what the calls carried.

**It is not a deprecation sweep.** Some of what this step removes is also
deprecated; that is a coincidence of timing and not the reason. Symbols this
step does not name may stay where they are even if a compiler warns about them.

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
installed 3.2.5. No copy of a library travels with the tree.

### The curve is built from its parameters

**`EC_GROUP_new_from_params` is how this engine makes a group.** It is called,
and it is what runs: not a call site behind a branch the work does not take,
with the hand-built path still there for everything else.

**The two calls that build a group by hand are gone.**
`EC_GROUP_new_curve_GFp` and `EC_GROUP_set_generator` are not called, not
imported, and not reached at run time.

`EC_POINT_set_affine_coordinates_GFp` is **not** in that list. It is not
banned and it is not asked about here.

**Every group is built from the TPM's own curve data.** Every call carries the
field's prime, a generator and an order — the numbers this tree's own curve
table holds — and every call returns a group.

This is the requirement that a shortcut fails, so it is worth saying plainly
what the shortcut is. `EC_GROUP_new_from_params` also accepts a **group
name**, and naming a curve is three lines shorter than describing one. It is
also a different statement. This TPM implements three curves and the vendor
table this tree carries is the authority on all three; a group built from a
name is a group built from OpenSSL's copy of somebody's curve, and one of this
TPM's three — `BN_P256` — is a curve OpenSSL has no name for at all, as this
tree's own source comments already record. A port that takes the shortcut
either loses that curve or carries two code paths for what is one thing.

**The new call carries this engine's library context and property query.**
`EC_GROUP_new_from_params` takes both, and both are the ones this tree already
established: the context this engine made for itself, and a query that names an
implementation rather than leaving the choice to provider load order. The
three-call form had nowhere to put either, which is the only reason these two
group constructions were not already covered.

### RSA's options are a list

**None of the typed `EVP_PKEY_CTX` setters survives.** These six are what this
tree used, and none of them is called, imported, or reached at run time:

| call | what it set |
| --- | --- |
| `EVP_PKEY_CTX_set_rsa_padding` | the padding mode |
| `EVP_PKEY_CTX_set_rsa_oaep_md` | OAEP's digest |
| `EVP_PKEY_CTX_set_rsa_mgf1_md` | MGF1's digest |
| `EVP_PKEY_CTX_set0_rsa_oaep_label` | the OAEP label |
| `EVP_PKEY_CTX_set_rsa_pss_saltlen` | the PSS salt length |
| `EVP_PKEY_CTX_set_signature_md` | the signature digest |

`EVP_PKEY_CTX_ctrl` and `EVP_PKEY_CTX_ctrl_str` are held to the same rule
although this tree has never called either. They are the same route with the
prototype taken off — an integer command number and a `void *` — and reaching
for one of them to get a typed setter's name out of the sources would be a step
backwards dressed as this one.

**The options go in as a parameter list instead.** At least one list reaches
the operation. Both spellings are the same thing and either is fine: the
settings passed to `EVP_PKEY_encrypt_init_ex` / `EVP_PKEY_verify_init_ex`, or
an ordinary `init` followed by `EVP_PKEY_CTX_set_params`.

**The lists say everything the calls said.** Across a run that exercises
RSASSA and RSAPSS verification and OAEP and RSAES encryption, the lists this
engine builds carry all three padding modes this engine uses, and carry a
digest, an MGF1 digest, an OAEP label and a salt length. A port that translated
the encryption path and left verification on some other route is not done.

**The PSS salt length keeps its meaning.** The sentinel the tree passes today
means *recover the salt length from the signature*. The parameter route says that as the number itself, or as the string `auto`
(or `auto-digestmax`, which on verification means the same). Two other strings are
accepted in the same place and neither means it. `digest` means *the salt is
exactly the digest's own length*, and `max` means *the longest salt this
modulus and this digest leave room for*. Both read like descriptions of what
this engine happens to do, and a description is not the sentinel: a port that
passes `digest` accepts the signatures whose salt is that length and rejects
every other one this engine verifies today, and a port that passes `max`
rejects even the signatures this engine itself produces, because the salt it
produces is the digest's length and not the longest that would fit. Pass the
sentinel, not a description of it — and not the number a description works out
to either: a salt length computed from the digest is `digest` with the name
filed off, and it is wrong in the same way for the same reason. Wherever this
tree means the sentinel today, the sentinel is what has to arrive.

**The OAEP label's ownership flips, and the allocation that served the old
owner goes with it.** `EVP_PKEY_CTX_set0_rsa_oaep_label` **takes** the buffer
it is given, so a caller of it has to hand over a buffer of its own. A
parameter list is **read**: the provider copies what it needs, and the buffer
stays the caller's. So the parameter can point straight at the label this
function was handed, and a buffer allocated to feed the old owner has no owner
left. A port that allocates one anyway and points a list at it leaks it once
per OAEP encryption, and produces the correct ciphertext while doing so. When
this engine has finished, nothing it allocated from libcrypto's allocator is
still live.

### Everything else

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
meaning of every return value stay as they are. Whatever this step needs is the
engine's own; `CryptoEngine.h` is what the engine publishes, and nothing this
step asks for needs a new entry in it.

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
made by `_cpri__InitCryptoUnits` and released by `_cpri__StopCryptoUnits`, with
nothing the engine fetched before that context existed still in use after it;
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
itself, and decryption no longer goes through it —; none of the
deprecated `EC_KEY`, `ECDSA_*` or low-level `RSA_*` and `SHA*`/`AES_*` families
anywhere in the sources, the imports or the run; the FIPS module loaded into the
engine's own context from a configuration loaded into that same context,
self-tested with the result acted on, held for the life of the engine and given
back beside the default provider at shutdown; and every fetch and every
key-operation context naming the implementation it wants rather than inheriting
one. A step that moves forward by undoing one of those has not moved forward.

Three of them are where this step lands, so they are worth spelling out:

* **The keys are still built the same way.** This step changes what an
  `EVP_PKEY_CTX` is *told*, not how it is made: `EVP_PKEY_CTX_new_from_pkey`
  still takes the engine's property query, `EVP_PKEY_fromdata` still builds
  every asymmetric key from an `OSSL_PARAM` list in the engine's context, and a
  key-operation context is still made for the operation that needs it and freed
  before the call that made it returns.
* **The property query is still named at every fetch.** This step adds a call
  that takes one; it does not take one away from a call that already has it.
* **`EccCurveInit` still returns a group the rest of the engine can use, and
  still returns `NULL` for a curve this build does not implement.** Every caller
  of it is unchanged, and so is what happens when the curve is not there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
