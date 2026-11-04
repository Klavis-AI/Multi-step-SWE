# Step 9 — a symmetric key is an object, not a pointer to bytes

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, makes a library context of its own,
loads the default provider and the FIPS module into it, self-tests the module,
fetches every digest, every cipher and every public key out of that context
with a property query that names the implementation it wants, hands every
operation its options as a parameter list, draws its randomness from a
generator it named in its own context, and compiles with everything 3.4
retired fenced off.

This machine has OpenSSL 3.5.4 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

One thing in the engine has not moved since 1.0.2, and it is the argument
itself. Every symmetric operation still receives its key as a `const BYTE *`
handed to a cipher init. The library copies those bytes into the context and
the provider copies them again into its own key schedule; the only object in
the picture is the context, and the key is a buffer that three layers hold at
once. Everything around it has become an object with a provider behind it —
the digest, the cipher, the public key, the generator, the parameter list.
The symmetric key was the last thing 3.x had not given that treatment to, and
3.5 gave it: `EVP_SKEY`, made by a provider's key manager, handed to an init
as a handle.

That is this step. The key arrives at the cipher as an object.

**This is not a claim about secrecy.** On this release the default provider's
key manager will hand the bytes back to whoever holds the object, so nothing
here makes a key unreadable to the engine and nothing in this step should be
written as though it did. What changes is the *shape of the argument*, which
is what lets a provider that does keep keys somewhere else — a token, an HSM,
a FIPS module with its own storage — serve this engine without the engine
being rewritten a third time.

**This is not FIPS.** Which module answers is settled and is not revisited.
Worth knowing before it costs an afternoon: on this release the FIPS provider
registers no symmetric key manager at all, so a property query naming it
selects nothing rather than selecting a FIPS key object, and both the named
query and no query at all resolve to the same default-provider implementation.
Carry the query anyway, for the reason every other fetch in this engine
carries one; do not expect it to change which provider answers.

**This is not another deprecation sweep.** Nothing named here is deprecated.
The old spellings are supported, documented and correct 3.5 code. They are
being left behind because of what they take, not because of what they are
called.

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
build directory, and configuring any build directory of this project with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added still writes a
`compile_commands.json` describing what this tree compiles and with what flags.
Whether that option is on or off does not change what the project compiles or
how, and a configure that answers it with a different set of compile lines from
the configure that was built has described something other than this tree.

**OpenSSL comes from the system, and it is this one.** Every object the build
produces resolves its OpenSSL symbols through the dynamic loader at run time and
defines none of them itself, and the libcrypto the loader resolves is the
installed 3.5.4. No copy of a library travels with the tree.

### No cipher context in this tree is started from key bytes

**Not called, not written down, not imported.** No function that starts an
operation on an `EVP_CIPHER_CTX` by taking the key as a pointer to bytes is
called anywhere in the sources or appears in their text — including behind a
macro or as a string handed to a loader — and none is in the dynamic import
table of any of the four built objects. That set is whatever the
installed headers say it is; it is not a list carried in this step, and it is
larger than the family whose names end in `Init_ex`.

**A 3.5-only spelling that takes bytes is still bytes.** Two of the entry
points in that set are new in this release, are not deprecated, and take a
length — they look exactly like what this migration has been moving towards. They
take `const unsigned char *key`. Being newer than the call it replaces is not
what makes a call migrated; what the call takes is.

**Every symmetric operation the engine performs carries a key object.** Not
most of them, and not the ones on the path a reader reaches first: a second
route into the same work that still passes bytes is the thing this requirement
is about. If a fallback exists, it is a route.

### The key object is this engine's own

**It is made in this engine's library context, and it names a property query.**
The import will accept `NULL` for both, and neither is what this step asks
for: the query has to be there, and it has to be one that selects the same
implementation the ciphers in this file are already fetched with. The *init*
will not accept a mismatch: a key object and a cipher that came from two
different library contexts are refused, so every cipher this file uses has to
come from the engine's own context — none fetched before that context existed.
This is not a style point — it is what makes the two halves of the init match.

**The key length is a property of the object.** Under this API the length is
fixed when the object is made, and a cipher whose key length disagrees with it
is refused at the init rather than half-way through. There is nothing to pass
at the init and nothing to keep in step; the size that used to travel beside
the pointer travels inside the object.

### The IV goes over, and so does its length

**The init takes the IV and its length as two arguments, and has no default for
the second.** The call being replaced took only the pointer and read the length
off the cipher itself, so this is a new argument with no old one behind it, and
the two ways of getting it wrong do not behave the same way:

* An IV with a length that is not the cipher's is refused — the init returns 0
  and says so. Loud, and the engine's own failure path takes it from there.
* No IV at all is accepted, and the operation runs from a zero register. That
  is the one difference from the call being replaced that compiles, runs,
  returns success and produces a different answer. A round trip inside the
  engine still agrees with itself, because both directions start from the same
  wrong place.

**So the length is the cipher's, and every mode that has an IV passes both.**
A mode with no IV passes neither, and that is a length of zero arrived at
honestly rather than a length left unset.

### A key object is released on every path

**The init takes no reference on it.** The context does not keep the object
alive; the caller that made it still owns it after the init, and after the
operation. Giving it back once the context is done with it is both necessary
and safe.

**Including the path where the operation failed.** A failure in this engine is
not a return: `FAIL()` is a long jump into the TPM's failure mode, and the code
after the test does not run. The simulator keeps running afterwards, so what a
path like that leaves behind accumulates.

**Including the path where there was no context to put it in.** An allocation
that failed between making the object and using it is still a path the object
was made on.

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
step asks for needs a new entry in it. In particular, a key object is an
implementation detail of the operation that made it and does not appear in the
engine's interface, its callers or its tables.

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
no digest computed below EVP; every symmetric operation's data still moving
through EVP with the low-level cipher families gone; every asymmetric key built
with `EVP_PKEY_fromdata` from an `OSSL_PARAM` list, in that same context, and
freed by the call that built it; the public halves of RSA and ECDSA driven
through `EVP_PKEY_encrypt`, `EVP_PKEY_verify` and `EVP_PKEY_sign`, with the
hand-rolled public-side padding encoders gone and the private-side ones still
compiled and still reached from the entry points that own them; the private halves still the engine's own constant-time exponentiation through
`RSADP`, on a path of decryption's own — the engine's published
modular-exponentiation entry point does not take the constant-time path
itself, and decryption no longer goes through it —; the
FIPS module loaded into the engine's own context from a configuration loaded
into that same context, self-tested with the result acted on, held for the life
of the engine and given back beside the default provider at shutdown; every
fetch and every key-operation context naming the implementation it wants rather
than inheriting one; every one of the TPM's curves built by
`EC_GROUP_new_from_params` from the vendor table's own parameters, in the
engine's context and with its query; RSA's padding mode, digests, OAEP label and
PSS salt length travelling as a parameter list, with the salt-length sentinel
still meaning *recover it from the signature*; the tree still compiling, in a
configure of its own with `-DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30400000L
-DOPENSSL_NO_DEPRECATED"` and `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added, with
the fence satisfied rather than switched off — no warning flag, diagnostic
pragma or language standard older than C99, no `#undef` or `#define` of either
fence macro anywhere in the tree, no `#define` of a retired name and no
prototype written for one, no directory of the tree passed as a system include
directory, nothing put in front of every translation unit, and the system
compiler with no launcher, wrapper, plugin or response file — and no retired
name called or imported; the
four process-global randomness routes gone, both draws taking the engine's
context and stating a security strength, the generator named on that context by
a call whose return value is checked, and the platform's entropy reaching that
generator as additional input; and nothing this engine allocates from
libcrypto's allocator still live when it is done. A step that moves forward by
undoing one of those has not moved forward.

Three of them are where this step lands, so they are worth spelling out:

* **The ciphers are still fetched by name from the engine's context, and still
  held.** This step changes what is handed to the init. It does not change
  where the cipher came from, and it does not turn a held cipher into a
  per-operation fetch.
* **The data still moves through EVP.** The key changing shape is not a reason
  for anything to go back below it.
* **The fence is still up, at `-DOPENSSL_API_COMPAT=0x30400000L
  -DOPENSSL_NO_DEPRECATED`.** Code added by this step is behind it too, and a
  name that has to be implicitly declared to compile is not a name this tree
  calls.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
