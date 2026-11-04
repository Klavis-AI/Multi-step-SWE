# Step 5 — the public half of RSA and of ECDSA, moved onto EVP_PKEY

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, draws its secrets from the private
generator, creates a library context of its own in `_cpri__InitCryptoUnits`,
loads a provider into it by name, fetches every digest it hashes with and every
cipher it encrypts with out of that context, and holds them.

The asymmetric half is where the 1.0.2 tree still is, and it is there in two
different ways.

`CpriECC.c` uses `<openssl/ec.h>`'s key object: `EC_KEY_new()`, a group set on
it, a private scalar or a public point set on it, and then `ECDSA_do_sign()` or
`ECDSA_do_verify()` to do the work. All of that is `OSSL_DEPRECATEDIN_3_0`. A
key built that way has no library context, so nothing the engine's own context
decides — which provider, which implementation, whether the algorithm is
allowed at all — reaches it.

`CpriRSA.c` does not use libcrypto's RSA object at all. It exponentiates
`BIGNUM`s directly and writes the padding out by hand: OAEP with its own MGF1,
PKCS#1 v1.5 for encryption, PSS, and the `DigestInfo` encoding for signatures —
roughly six hundred lines of encoder and decoder. There is nothing deprecated
about `BN_mod_exp`. The problem is one step further out: four byte strings in a
TPM structure and a hand-written mask are not a key a provider can be asked
about, so `CpriRSA.c` has no provider either.

That is the gap this step closes. Until the asymmetric keys are objects the
library owns, there is nothing for a provider to be asked about: you cannot ask
a provider for a policy on a stack object or a hand-written mask, and a
`BIGNUM` exponentiation is not an algorithm a provider implements — it is
arithmetic.

At 3.0 a key is built the same way for every algorithm: an `OSSL_PARAM` list
describing it, a context made from a library context and an algorithm name, and
`EVP_PKEY_fromdata`. What comes back is an `EVP_PKEY`, and the operations —
`EVP_PKEY_sign`, `EVP_PKEY_verify`, `EVP_PKEY_encrypt` — are driven through an
`EVP_PKEY_CTX` made from it, with the padding scheme named as a parameter
instead of written out.

This machine has OpenSSL 3.0.17 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

**This step moves the public half and only the public half.** Four entry points:
`_cpri__EncryptRSA`, `_cpri__ValidateSignatureRSA`, `SignEcdsa` and
`ValidateSignatureEcdsa`. `_cpri__DecryptRSA` and `_cpri__SignRSA` stay exactly
as they are, on `RSADP` and on the engine's own constant-time exponentiation,
because that is a guarantee an earlier step asserted on this tree and handing
the private key to a provider would un-assert it. Section 2 says which of the
padding encoders go with the public half and which stay with the private half;
the split is exact and it runs in both directions.

The bookkeeping is the easy half. The work is in two places.

The first is the EC curves. They live in `CpriDataEcc.h` as explicit parameters
— field prime, `a`, `b`, generator, order, cofactor — and one of them has no
name libcrypto knows: `BN_P256` is the Barreto–Naehrig curve ECDAA needs, and
there is no NID for it. A port that builds keys from curve names works for two
of the three curves this build enables and cannot express the third. Nothing here forbids the
name: wherever libcrypto has one for the curve, a named group says which curve
this is perfectly well. The point is only that one of the three has none, so the
explicit parameters have to work too.

The second is that this engine's answers are not the library's answers. The
hand-written encoders decided several things before they looked at the data, and
they decided them in an order that is part of the answer this tree gives: which digests
exist, what "message too long" returns, and what a signature that is
numerically larger than the modulus returns. EVP is stricter in some places and
laxer in others, and every difference is a wrong answer that builds, links,
runs and round-trips against itself. Section 2 states each one, because nothing
the tree does to itself will catch them.

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
installed 3.0.17. No copy of a library travels with the tree.

**Every key EVP operates on was built from parameters.** Each of the four entry
points — `_cpri__EncryptRSA`, `_cpri__ValidateSignatureRSA`, `SignEcdsa`,
`ValidateSignatureEcdsa` — builds the key it needs with `EVP_PKEY_fromdata`,
after `EVP_PKEY_fromdata_init`, from an `OSSL_PARAM` list, on a context
obtained from `EVP_PKEY_CTX_new_from_name`. Those three names are called
somewhere the build compiles. The list itself may be built either way the
library offers — with `OSSL_PARAM_BLD` and `OSSL_PARAM_BLD_to_param`, or
written out as an `OSSL_PARAM` array closed with `OSSL_PARAM_construct_end` or
with the `OSSL_PARAM_END` terminator — and the two routes are equally acceptable
here. An entry point may build its key directly or through a helper it reaches;
what matters is that it is reached.

That context names the library context `_cpri__InitCryptoUnits` makes. Not the
one the process happens to share: passing `NULL` there means the process
default, which is where this engine's algorithms came from before it had a
context of its own, so a `NULL` there has changed the spelling and not the
source. (`EVP_PKEY_CTX_new_from_pkey`, handed a key this engine has just built
in its own context, is not held to this sentence.)

**A key is not an algorithm, and is not cached.** This reverses a requirement
two earlier steps make, so it is worth being explicit about why. A digest and a
cipher are algorithms: there is one SHA-256 for the life of the engine, and
those steps require that it is fetched once and held, and that `EVP_MD_free` and
`EVP_CIPHER_free` are *not* reachable from the entry points that do work. A key
is a value. It arrives in the caller's argument list on every call and there is
nothing to hold it for. So the requirement here is the other one: each of the
four entry points can reach `EVP_PKEY_free` — every one of the four, not some
of them — and
how many keys the engine builds
grows with how much asymmetric work it does — a run that performs many
asymmetric operations builds more keys than a run that performs few, at a rate
of at least one key per two operations. Both are true of the obvious
implementation, which builds a key where it needs one and frees it before it
returns. Neither is true of a port that builds one key at startup, and an
engine that performs no asymmetric operations satisfies none of this by
omission.

Nothing is left behind, and nothing is kept back for the end. Every `EVP_PKEY`
the engine builds is freed, and every `EVP_PKEY_CTX` it makes — from a name or
from a key — is given back with `EVP_PKEY_CTX_free` when the operation that
made it is finished. That is the same rule the key is under, for the same
reason: a context belongs to the call that made it and does not outlive it.
Collecting them and freeing the lot at shutdown is a different arrangement and
is not what this step asks for: as with the keys, the number of contexts given
back grows with the amount of asymmetric work the engine does, at a rate of at
least one context freed per two operations, and none of them are still live
when the engine stops. The parameters go the same way, by whichever route
built them: a builder is released with `OSSL_PARAM_BLD_free` and the list it
handed back with `OSSL_PARAM_free`, while a list written out as an
`OSSL_PARAM` array — closed with `OSSL_PARAM_construct_end` or with the
`OSSL_PARAM_END` terminator, whichever reads better — allocates nothing and so
releases nothing. Either route is fine; half of one is not.

**RSA's public operations are the library's.** `_cpri__EncryptRSA` reaches
`EVP_PKEY_encrypt` and `_cpri__ValidateSignatureRSA` reaches `EVP_PKEY_verify`,
and at run time those calls carry the data: for every RSA public-key encryption
the engine performs, the message goes to `EVP_PKEY_encrypt` at least once, and
for every RSA signature it checks, the signature goes to `EVP_PKEY_verify` at
least once, so a run that does a lot of RSA makes a lot of both calls. Reaching
the call and then not taking that route with real data is not moving the
operation, and a call the compiler can see but the run never arrives at is
exactly that. The
padding is named to the operation as a parameter rather than applied before it,
so neither of those two functions can still reach the engine's own
`_cpri__MGF1`.

`_cpri__EncryptRSA`'s unpadded case is not padding, has no scheme to name, and
is not part of this: `TPM_ALG_NULL` treats the input as a number, and the
exponentiation it needs is the one this file has always done.

**ECDSA is the library's.** `SignEcdsa` reaches `EVP_PKEY_sign` and
`ValidateSignatureEcdsa` reaches `EVP_PKEY_verify`, and at run time those calls
do the work: for every ECDSA signature the engine produces, the digest goes to
`EVP_PKEY_sign` at least once, for every one it checks the signature goes to
`EVP_PKEY_verify` at least once, and the deprecated one-shot signing and
verification entry points named below are not called at all — not once, over a
run that signs and verifies repeatedly. Neither function reaches `EC_KEY_new`
any more, and neither has taken back the two things an earlier step already took
out of them by hand: no `BN_mod_inverse` and no `_cpri__EccPointMultiply` is
reachable from either. Doing the modular inverse or the point multiplication
yourself is the one way to satisfy every ban below and still not have moved the
algorithm.

**The deprecated EC key API is gone.** None of it appears in the sources the
build compiles, in what the built engine asks the loader for, or in what runs.
Named exactly: `EC_KEY_new`, `EC_KEY_new_by_curve_name`, `EC_KEY_free`,
`EC_KEY_dup`, `EC_KEY_set_group`, `EC_KEY_get0_group`, `EC_KEY_set_conv_form`,
`EC_KEY_set_public_key`, `EC_KEY_get0_public_key`, `EC_KEY_set_private_key`,
`EC_KEY_get0_private_key`, `EC_KEY_set_public_key_affine_coordinates`,
`EC_KEY_generate_key`, `EC_KEY_check_key`, `EC_KEY_set_asn1_flag`,
`EC_KEY_precompute_mult`, `ECDSA_do_sign`, `ECDSA_do_sign_ex`,
`ECDSA_do_verify`, `ECDSA_sign`, `ECDSA_sign_ex`, `ECDSA_sign_setup`,
`ECDSA_verify` and `ECDSA_size` — and `EC_KEY`. That last one is a type rather
than a function, and it is on the list for the same reason as the rest: it is
the object the whole sequence is built around, and a tree that still declares
one has kept the API whatever else it did.

Three things are **not** on that list and are not going anywhere. `EC_GROUP`,
`EC_POINT` and `BN_CTX` are not deprecated at 3.0, they are how
`_cpri__EccPointMultiply` and the curve setup are written, and this step does
not touch them. `ECDSA_SIG` with `i2d_ECDSA_SIG` and `d2i_ECDSA_SIG` is not
deprecated either, and it is the supported way to get at the `r` and `s` of an
ECDSA signature — which is what this interface returns, and what
`EVP_PKEY_sign` hands back a DER `SEQUENCE` of. And the TPM layer above
the engine has names of its own that begin the same way — `ECDSA_VALUE`,
`ECDSA_Marshal`, `ECDSA_Unmarshal`. The list above is exact names, not prefixes.

**The low-level RSA API is gone too, and it was never here.** This engine does
not use `<openssl/rsa.h>`'s object today, and it does not start. It is named
because "call `RSA_public_encrypt` and let libcrypto do the OAEP" is the wrong
answer that looks most like the right one: it is a key with no library context,
exactly like the EC one. None of these appears in the sources, the imports or
the call graph: `RSA_new`, `RSA_free`, `RSA_set0_key`, `RSA_set0_factors`,
`RSA_get0_key`, `RSA_public_encrypt`, `RSA_private_decrypt`,
`RSA_public_decrypt`, `RSA_private_encrypt`, `RSA_sign`, `RSA_verify`,
`RSA_size`, `RSA_generate_key_ex`, `RSA_padding_add_PKCS1_OAEP`,
`RSA_padding_check_PKCS1_OAEP`, `RSA_padding_add_PKCS1_OAEP_mgf1`,
`RSA_padding_check_PKCS1_OAEP_mgf1`, `RSA_padding_add_PKCS1_type_1`,
`RSA_padding_check_PKCS1_type_1`, `RSA_padding_add_PKCS1_type_2`,
`RSA_padding_check_PKCS1_type_2`, `RSA_padding_add_PKCS1_PSS`,
`RSA_verify_PKCS1_PSS`, `RSA_padding_add_PKCS1_PSS_mgf1` and
`RSA_verify_PKCS1_PSS_mgf1`. The padding macros those take — `RSA_PKCS1_PADDING`
and the rest — are what `EVP_PKEY_CTX_set_rsa_padding` is given, and are fine.

Gone from the sources means gone from their text. A call that is still written
out but switched off — parked in a branch the preprocessor never takes, or
behind a macro nothing defines — is code this step removes rather than code it
allows, and putting it out of the compiler's reach is not taking it out of the
tree. A comment is the exception in the other direction: prose that mentions a
name the engine no longer calls is a note about the migration, not a use of it.
Asking the loader for one of these names at run time is still asking for it: an
engine that links what it calls has no use for `dlopen`, `dlsym` or `dlvsym`,
and none of the three appears in the sources the build compiles. That holds for
every name this instruction says is gone.

**The public half of the padding goes; the private half stays.** This is the
requirement most likely to be got half right, because the natural reading of
"delete the hand-rolled padding" deletes twice as much as this step asks for.

Each scheme has two ends, and they are on opposite sides of the key. OAEP and
PKCS#1 v1.5 *encryption* padding is applied by whoever encrypts — the public
side — and removed by whoever decrypts, which is the private side. PSS and the
PKCS#1 v1.5 *signature* encoding are applied by whoever signs — the private
side — and removed by whoever verifies, which is the public side. So:

| deleted, with the public half | stays, with the private half |
| --- | --- |
| `OaepEncode` | `OaepDecode` |
| `RSAES_PKSC1v1_5Encode` | `RSAES_Decode` |
| `PssDecode` | `PssEncode` |
| `RSASSA_Decode` | `RSASSA_Encode` |

The four on the left have no definition left in the tree. The four on the right
still have one, and each is still reached from the entry point that owns it:
`_cpri__DecryptRSA` reaches `OaepDecode` and `RSAES_Decode`, and `_cpri__SignRSA`
reaches `PssEncode` and `RSASSA_Encode`. Both of those two also still reach
`RSADP`. Compiling the four on the left and never calling them is not deleting
them; deleting the four on the right is a different migration from this one.

**The behaviour of the four entry points is unchanged, and six of their answers
do not survive by accident.** Everything in the next subsection is something
this tree does today and the library would do differently. Each is a wrong
answer that builds, links, runs and round-trips against itself.

* **An unsupported digest is refused before the message length is looked at.**
  This build does not implement every hash the interface can name, and
  `_cpri__GetDigestSize` answers 0 for the ones it leaves out. `OaepEncode`
  tested that *first*, so `_cpri__EncryptRSA` asked for OAEP with an
  unimplemented hash returns `CRYPT_PARAMETER` — not "message too long", and
  not a success. Keep the order, and keep the arithmetic: the digest size is
  signed, the sizes it is compared against are unsigned, and the message-length
  test is the original mixed-sign expression.
* **A message that does not fit returns `CRYPT_PARAMETER` for RSAES and
  `CRYPT_FAIL` for OAEP.** Those are two different answers from the two
  encoders, on two different comparisons, and both are what this tree answers
  today. Neither is
  what the library returns for an oversized message.
* **`CRYPT_SCHEME` and `CRYPT_PARAMETER` on an RSA signature are the signing
  encoder's answers, and the signing encoder is not moving.** `RSASSA_Encode`
  refuses a hash with no DER prefix with `CRYPT_SCHEME` and a digest whose
  length is not that hash's own with `CRYPT_PARAMETER`, and it does both before
  `_cpri__SignRSA` gets as far as the exponentiation. Those two answers come
  from the signing path and nowhere else, so the way to keep them is to
  leave that half where it is: `_cpri__GetHashDER` is still the authority on
  which digests this engine signs with.
* **On the verify path every failure is `CRYPT_FAIL`, whatever it was.** A
  signature that does not match, a signature that is not the modulus's length,
  and a signature numerically not less than the modulus are three different
  events in `_cpri__ValidateSignatureRSA` and one answer: the length
  test, the exponentiation refusing an out-of-range base, and the caller
  turning any non-success into `CRYPT_FAIL` all arrive at the same byte. EVP
  tells those apart and this tree's answer does not, so a port has to put them back
  together.
* **A PSS salt size of zero means "whatever the signature says".** `PssDecode`
  compared a recovered salt length against the caller's only when it had been
  given one, and zero is what the TPM layer's own call to
  `_cpri__ValidateSignatureRSA` gives it. So the moved
  verify path recovers the length from the signature rather than demanding one.
  MGF1 uses the signature hash.
* **The OAEP label carries its terminating NUL.** `OaepEncode` hashed
  `strlen(label) + 1` bytes. That is this TPM's convention and nobody else's,
  and it is what the private half that stays in the tree will expect to find. A
  port that hands the library the string without its terminator produces
  ciphertext this tree's own decryption cannot open.

And two about ECDSA, where both mistakes are the same shape — giving the
operation something the old code did without:

* **The two ECDSA functions truncate the digest themselves, by two different
  rules.** `SignEcdsa` truncates to the length of the curve's order.
  `ValidateSignatureEcdsa` truncates to the length of the `r` it was handed,
  which is not the same quantity — a signature whose `r` has a leading zero byte
  is rejected here today, and has to keep being rejected. Both rules survive
  only if the operation is given no signature digest: a provider told which
  digest a signature is over refuses input that is not exactly that digest's
  length, and one that is not told takes the bytes it is given. Do not set one.
* **P-384 ECDSA signing still fails, fatally.** `MAX_ECC_PARAMETER_BYTES` is 32
  and `SignEcdsa`'s assertion on the order's size therefore cannot hold for a
  48-byte order. That is a fatal error this tree raises today, on the signing
  path only — P-384 *verification* works today and keeps working. A port that
  "fixes" this by
  widening the constant has changed two answers that were not asked about.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are. In particular the four entry
points keep their names, their parameters in their order, and their results;
`_cpri__EncryptRSA` still writes the modulus-sized ciphertext and reports its
size, and `SignEcdsa` still writes `r` and `s` left-padded to the order's
length.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served.

**Behaviour is unchanged.** For every input, the engine and the simulator built
from this tree produce exactly what they produce today: the same ciphertext for
the same key, IV and plaintext, the same outgoing IV, the same digests, the same
signatures, the same public keys for the same seeds, the same response codes,
the same error and failure-mode behaviour on bad input. A change in an answer is
a defect in the port, whatever else improved. Where an answer is random, what
has to stay the same is its shape — how much output the operation produces, and
that separate draws remain independent of one another — not the bytes. An
ECDSA signature and an OAEP or RSAES ciphertext are in that category: they are
randomised, and what has to hold is that they verify and decrypt.

One thing a fatal error carries is not part of that answer. `TpmFail` is handed
the source position that raised the failure — a function name and a `__LINE__`
— and any edit above a failure site moves the line, so every port moves some of
them. What has to match is the fatal code the engine chose, and that the TPM is
in failure mode at all. The source position is not part of it, including where a
failure-mode `TPM2_GetTestResult` hands it back: it records where the source
was, not what the engine did.

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
a provider loaded into it by name and unloaded from it. A step that moves forward by undoing one of those has not moved forward.

Three of them are where this step lands, so they are worth spelling out:

* **The private-key exponentiation is still the engine's, and still told its
  exponent is secret.** `_cpri__DecryptRSA` and `_cpri__SignRSA` still reach
  `RSADP`, and `RSADP` and the modular exponentiation under it still set the constant-time flag and use the
  constant-time exponentiation. That exponentiation is decryption's own: the
  engine's published modular-exponentiation entry point does not take the
  constant-time path itself, and decryption no longer goes through it. This is the
  requirement this step is most likely to break by doing *more* than it asks:
  moving the private key onto `EVP_PKEY` too would hand that property to a
  provider, which is a reasonable thing to want and is not this step.
* **The digests and the ciphers are still fetched by name from the engine's own
  context, and still held.** Making room for a key builder means touching files
  that fetch, and a digest or cipher that starts being fetched per operation
  because the caching got reorganised fails a requirement the tree already
  holds. It still holds here, and so does everything that came with it: no `EVP_sha*`, no `EVP_sm3`, no `EVP_aes_*`, no
  `EVP_get_digestbyname`, `EVP_get_digestbynid`, `EVP_get_cipherbyname` or
  `EVP_get_cipherbynid`, no low-level `SHA*`/`AES_*` family, no `AES_KEY`, none
  of the accessors that used to reach a digest's private state —
  `EVP_MD_CTX_md`, `EVP_MD_CTX_get0_md_data`, `EVP_MD_meth_get_app_datasize` —
  in the sources the build compiles or in what the built engine imports, the
  count of fetches does not grow with the work, and every digest and cipher the
  engine fetched is given back when the engine stops. If you need an `EVP_MD *`
  to name a digest to an operation, it comes from the ones the engine already
  fetched.
* **ECDSA is still signed and verified by libcrypto.** An earlier step moved it
  off hand-written arithmetic and onto libcrypto's own ECDSA; this step moves it
  from libcrypto's deprecated entry point to its current one. It does not move
  back. The two negative clauses above — no `BN_mod_inverse`, no
  `_cpri__EccPointMultiply` reachable from either ECDSA function — are that
  requirement, unchanged.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
