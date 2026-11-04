# Step 8 — nothing retired, and the randomness comes from the engine's own context

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) builds against an installed OpenSSL, reaches
OpenSSL's types only through accessors, makes a library context of its own,
loads the default provider and the FIPS module into it, self-tests the module,
fetches every digest, every cipher and every public key out of that context
with a property query that names the implementation it wants, and hands every
operation its options as a parameter list.

This machine has OpenSSL 3.4.2 installed as the system OpenSSL
(`openssl version`). No other OpenSSL is installed and none is available.

Two things are left, and they are the same thing looked at from two sides:
what the tree still asks for by a name 3.x has retired, and what it still asks
for from somewhere other than its own context.

* **The fence.** `OPENSSL_API_COMPAT` names the release whose API a tree
  expects and `OPENSSL_NO_DEPRECATED` says that anything retired at or before
  it should not merely warn — it should not be declared at all. Built that
  way, this tree does not compile. That is the compiler's own answer to "is
  this written against 3.4", and it is a stronger answer than a warning count,
  because some of what it catches has no symbol and emits no warning.

* **The randomness.** Every algorithm file in this engine carries the engine's
  library context into its fetches — every one but one. That file's four entry
  points still go to the four process-global routes — seed, stir, public draw,
  private draw — so the engine loads the FIPS module into a context of its own,
  self-tests it, and then draws from whatever generator the *process default*
  has. Which generator that is, is decided by which provider happened to load
  first. Nothing in the sources says so, no compiler flag can see it, and the
  numbers that come out are random either way.

**This is not FIPS enforcement.** Which provider answers is not this step's
subject and is not revisited. Nothing here asks the engine's generator to come
from a particular module; what it asks is that the engine *named* one, out of
its own context, in time for the naming to mean anything.

**This is not a seeding policy.** How much entropy the platform hands over and
how often is the platform's business and does not change. What changes is
where those bytes go.

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
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added — this one again afterwards, or the
fence configure described below, which carries it from the start — still writes
a `compile_commands.json` describing what this tree compiles and with what
flags. Whether that option is on or off does not change what the project
compiles or how.

**OpenSSL comes from the system, and it is this one.** Every object the build
produces resolves its OpenSSL symbols through the dynamic loader at run time and
defines none of them itself, and the libcrypto the loader resolves is the
installed 3.4.2. No copy of a library travels with the tree.

### The tree builds behind the deprecation fence

**A configure of this same project with the fence added compiles every
object.** That is a third configure, into a build directory of its own, with

```
-DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30400000L -DOPENSSL_NO_DEPRECATED"
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

added and nothing else changed. All four objects have to come out of it, and
the `compile_commands.json` that configure writes is the record of how they
were compiled.

**And building is not enough, because C compiles more than it declares.**
Behind the fence a retired *macro* is an undeclared identifier and the build
stops; a retired *function* is only an implicit declaration, which C still
compiles, so a tree can survive the fence while still calling one. What is
required is both halves: the tree compiles behind the fence, and no name
OpenSSL has retired at this API level is called or imported anywhere in it.
A call the build let through is still a call.

**The fence is satisfied, not switched off.** A warning flag, a diagnostic
pragma, an `#undef` or a `#define` of either fence macro anywhere in the tree, a
`#define` of a retired name, a prototype written in the tree for something the
header no longer declares, a language standard older than C99 (`-ansi`,
`-std=c89`, `-std=gnu89` and their aliases), under which calling an undeclared
function is not diagnosed at all: none of these changes the fact that the call is there, and none of them is
allowed. What is being asked is not which spellings are absent from the
build flags — it is whether the tree still asks OpenSSL for something OpenSSL
has withdrawn. That covers the routes that are not spellings in the tree's own
text: an `-isystem` or `-idirafter` naming a directory inside the tree, which
makes the compiler treat what it finds there as a system header and stop
diagnosing it; a header put in front of every translation unit with `-include`
or `-imacros`, which can hold any of the above and need not sit in the tree at
all; a compiler, a compiler launcher, a spec file, a plugin or a response file
that is not part of the installed toolchain, none of which can be read off the
command line they are named on. What the build is compiled with has to be
something a reader of this project can see.

**And the fence has to reach the compiler, and still be standing at the end of
the line.** With the two macros added as above, every compile line of that
build carries both of them and leaves both in force. The preprocessor takes the
last word, so a `-U` of either, or a later `-D` putting `OPENSSL_API_COMPAT` at
some other level, undoes the fence exactly as thoroughly as never passing it. A
build that drops, replaces or undoes what it was configured with has not been
fenced, and nothing it did or did not say about a retired name means anything.

**Nothing the tree links against is a retired function.** Independently of the
sources: none of the four built objects has a deprecated OpenSSL function in
its dynamic import table. The deprecated set is whatever the installed headers
say it is at this API level; it is not a list carried in this step.

Where a retired call has a modern spelling with the same arguments, the
spelling is the migration. Where it does not, the call site is the migration:
`BN_check_prime` takes no checks argument, so what this tree passes as one goes
away rather than being translated — the modern call chooses the number of
rounds from the size of the candidate, which is what the constant was a fixed
approximation of. Both call sites already branch on "negative is an error, 1 is
prime", which is `BN_check_prime`'s contract too.

### The randomness comes from the engine's own context

**The four process-global routes are gone.** `RAND_seed`, `RAND_add`,
`RAND_bytes` and `RAND_priv_bytes` are not called in the sources, not imported
by any of the four objects, and not reached at run time. None of the four is
deprecated; they are on this list because each of them is a route to a
generator this engine does not own.

**The draws take the engine's context.** `RAND_bytes_ex` and
`RAND_priv_bytes_ex` replace the two draws, and the context they are given is
the one this engine made for itself — not `NULL`, which is the same global
generator with a longer name and is indistinguishable from the migrated call in
any reading of the source.

**Every draw states a security strength.** The fourth argument is what the
bytes have to be worth and `0` asks for nothing. On this build a CTR-DRBG over
AES-256 satisfies 256 and refuses 257, so a stated strength is a real
constraint over a real range rather than a field that got filled in.

**The two pools stay two pools.** The public entry point draws from the public
pool and the private one from the private pool, as they do today: which entry
point a caller picks is still what says what kind of bytes it is asking for.

### The generator is one the engine named, and it is named in time

**`RAND_set_DRBG_type` is called on this engine's library context and it
succeeds.** That call is what decides which implementation serves a context's
DRBG chain. Without it the chain resolves to whichever provider loaded first,
which is not a decision this source makes.

**It only decides anything before the chain exists.** A DRBG chain is
instantiated on first use, and from then on the same call returns 0 and leaves
the chain exactly as it found it. A naming that arrives after the first draw
has written a line that does nothing, and the only place that says so is the
return value.

It follows that the call is a one-shot, and that decides *where* it goes. The
engine's startup paths are re-entered — a TPM starts up more than once in the
life of a process, and a power-on reset re-initialises the engine in the same
address space — so a naming written on one of them succeeds the first time
and returns 0 for the rest of the process, and a naming written on one of
them whose failure is treated as fatal breaks every startup after the first.
It belongs somewhere that runs once per process, before the chain exists, on
a path the engine does not re-enter: where the context itself is built.

**The return value is checked.** A call that quietly did nothing is the whole
failure mode here, and it reports itself in exactly one place.

### The seed reaches that generator

**The platform's entropy is handed to a DRBG this engine's context hands
back.** `EVP_RAND_reseed` is the 3.x call, and the generator it is given comes
from `RAND_get0_primary`, `RAND_get0_public` or `RAND_get0_private` on this
engine's context — the same chain the draws now come out of. Both paths that
add entropy today, the one at startup and the one the platform calls later, go
to it.

**The bytes go in as additional input, not as entropy.** `EVP_RAND_reseed`
takes those in two different arguments, and a DRBG that has a parent to draw
entropy from **refuses** caller-supplied entropy: the call returns 0, the
reseed counter does not move, and the bytes are gone. The same bytes in the
additional-input slot are accepted and mixed in. The slot named for what the
caller thinks it is doing is the slot that discards it.

**Every reseed succeeds.** Not "was written": returned 1.

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
back beside the default provider at shutdown; every fetch and every
key-operation context naming the implementation it wants rather than inheriting
one; every one of the TPM's curves built by `EC_GROUP_new_from_params` from the
vendor table's own parameters, in the engine's context and with its query; and
RSA's padding mode, digests, OAEP label and PSS salt length travelling as a
parameter list, with the salt-length sentinel still meaning *recover it from
the signature* and nothing this engine allocates from libcrypto's allocator
still live when it is done. A step that moves forward by undoing one of those
has not moved forward.

Two of them are where this step lands, so they are worth spelling out:

* **The private pool is still the private pool.** This step changes the
  spelling of both draws and the context they come from. It does not merge
  them: the engine's private draw still takes its bytes from the private pool, and
  rewriting it onto the public call is a step backwards dressed as this one.
* **The library context is still made once and released once.** This step adds
  a call that is made against it. It does not add a second context, and it does
  not move when the one context is built or freed.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
