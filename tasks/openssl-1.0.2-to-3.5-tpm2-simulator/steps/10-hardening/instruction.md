# Step 10 — the tree says what it is doing, and a fuzzer agrees

## 1. What this step is

Nine steps moved this engine from OpenSSL 1.0.2 to 3.5, construct by construct.
Every one of them was about a spelling: an opaque type, a context of its own, a
fetch by name, an EVP loop, a provider pair, a parameter list, a deprecation
fence, a key object. This one is not. Nothing here changes what the engine
computes or which OpenSSL call it makes. What changes is how much of the tree's
own correctness is stated out loud, where the compiler and the run-time can
check it instead of a reader.

Three things are asked for, and they overlap on purpose.

**The compiler's whole opinion, as errors.** The tree already builds behind
`-DOPENSSL_API_COMPAT=0x30400000L -DOPENSSL_NO_DEPRECATED`, which is one
question: is any name this tree uses retired at or before 3.4. Now it builds at
3.5 with `-Werror`, which is a different question — the first is a fact about
OpenSSL's headers, the second is a fact about the C this tree has been writing
since 1998. Some of what that surfaces is old: a function defined and exported
by one file and declared in no header at all, so every call to it is an
implicit declaration — legal in C89, a diagnostic since C99, an error here.
There are four of them, and the compiler names all four the first time the
build is run.

The engine's own files are held to more than that. Every translation unit the
engine library is built from also compiles under `-Wall -Wextra -Werror` — the
compiler's whole ordinary opinion, on the code these ten steps have been
rewriting. The rest of the tree is not asked for that, and the reason is
deliberate rather than an oversight: it is TPM reference source this migration
does not touch, its behaviour is held unchanged like everything else's, and
most of what `-Wall -Wextra`
finds in it is the indentation and the brace style of tables written in 2013.
Reformatting those is not what this step is about.

**The run-time's opinion, on everything the tree answers.** The tree is also
built by a configure of its own with `-fsanitize=address,undefined
-fno-sanitize-recover=all`, and that build is held to every answer the ordinary
one gives — the engine through its C interface and the simulator through its
TCP port, so whatever either of them reaches is in scope and not only the code
under `src/OsslCryptoEngine/`. Every shift that overflows its type, every misaligned
load, every read past the end of a buffer and every null pointer handed to a
function that promises not to take one becomes a report, and with
`-fno-sanitize-recover=all` the first report ends the process. There are such
sites in this tree today. They are not new and most of them do, in practice, on
this compiler, what their author meant — which is exactly what undefined
behaviour looks like right up until the day it does not.

**A fuzzing harness.** Something that turns an arbitrary run of bytes into a
call on this engine, so that the inputs it is asked about are not only the ones
somebody thought of. You write the half that decides what the bytes mean. The
loop that produces them is supplied, and is described below.

## 2. Requirements

### The tree builds with warnings as errors

**The ordinary build, and the OpenSSL it builds against.** This machine has
OpenSSL 3.5.4 installed as the system OpenSSL (`openssl version`). No other
OpenSSL is installed and none is available. The tree still configures and builds
out of tree with its own build system, unchanged in how it is driven:

```
cmake -S /app/cmake -B <build directory> -DSPEC_VERSION=116
cmake --build <build directory>
```

Those two commands and nothing on either of them that is not written there. The
build still produces all four of `platform/libplatform.so`,
`OsslCryptoEngine/libCryptoEngine.so`, `tpm/libTPM.so` and `Simulator` in the
build directory. Every object it produces resolves its OpenSSL symbols through
the dynamic loader at run time and defines none of them itself, and the
libcrypto the loader resolves is the installed 3.5.4. No copy of a library
travels with the tree. Configuring any build directory of this project with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added still writes a
`compile_commands.json` describing what this tree compiles and with what flags,
and whether that option is on or off does not change what the project compiles
or how — on the ordinary configure and on each fenced one below, which carry it
from the start.

Three things hold of the build with warnings as errors: it builds, the compile
lines it built from still say what the configure asked for, and the engine's
own files stand up to more.

**The tree.** Configuring `/app/cmake` with

```
-DSPEC_VERSION=116
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
-DCMAKE_C_FLAGS="-DOPENSSL_API_COMPAT=0x30500000L -DOPENSSL_NO_DEPRECATED -Werror"
```

produces `platform/libplatform.so`, `OsslCryptoEngine/libCryptoEngine.so`,
`tpm/libTPM.so` and `Simulator`, with no diagnostic of any kind. This is a
build of its own, in a directory of its own; the ordinary build above is
unchanged.

Those three flags have to appear on that build's own compile lines; having
handed them to `cmake` is not enough. Every line that compiles a file under `/app`
still carries `-Werror`, still carries `-DOPENSSL_NO_DEPRECATED`, and still
sets `OPENSSL_API_COMPAT` to 3.5; a `CMakeLists.txt` that appends `-Wno-error`
after them, or resets the flags for one directory, has not built the tree this
asks for. The compiler on those lines is the system compiler under `/usr`,
with nothing wrapped around it.

`-Werror` without `-Wall` is still the compiler's default opinion made fatal,
and on this tree that opinion is not empty: four calls reach a function with no
declaration in scope, in four different files, and the compiler names all four
the first time this build is run. Each is answered by a prototype in the right
header or an include that was never added — not by a cast, and not by a
declaration written at the call site: every call to one of the four resolves to
a declaration in a header, at file scope, or to the function's own definition,
never to one written into the calling source file or the calling function.

**The engine.** Every file that build compiles out of `src/OsslCryptoEngine/` —
and every other file it compiles into the engine's own library, wherever in the
tree that file has been put — also compiles clean when `-Wall -Wextra -Werror`
is added to the compile line that build used. Same defines, same include paths,
same everything else; the warnings are the only difference. Roughly forty
diagnostics stand between this tree and that, spread over the engine's sources
and the headers and data files they include, and every one of them is ordinary
C hygiene that the compiler states in full.

None of them is answered by changing what the engine computes. If a fix
changes an answer, it is the wrong fix — every answer the tree gives is still
held to what it is today, with or without the warning flags.

The fence moving from 3.4 to 3.5 is part of it. Nothing in this tree is retired
between those releases today, so on its own that is a line in a flag list; it is
asked for because a tree that builds against 3.5's headers should say 3.5.

### No warning is silenced

`-Werror` is a claim about what the compiler was allowed to say, and there are
seven ways to leave it standing while making it say nothing. None of them is in
this tree when the step is done:

* no `#pragma GCC diagnostic` or `#pragma clang diagnostic` of any kind — one
  that ignores a warning, and a push, a pop or a severity change as well — in
  either spelling, comments included, in any source or header either build
  reads —
  whether it is in the tree or generated into the build directory — no
  `#pragma GCC system_header`, and no `#include` naming a file that is
  neither in the tree nor in the system, which is how such a pragma reaches
  every translation unit while leaving nothing on the compile line;
* no `-w`, no `-Wno-…`, no `-Wno-error…`, no `-fpermissive` and no
  `--no-warnings` on any compile line either build produces;
* no `-isystem` or `-idirafter` naming a directory inside `/app`, which
  reclassifies the tree's own headers as system headers and suppresses
  everything in them at once, and no header search path of any spelling
  pointing at a directory that is neither the tree nor the system;
* no `no_sanitize` anywhere in the tree, comments included, and no `-fno-sanitize…`, no
  `-fsanitize-recover…` and no `-fsanitize-ignorelist`/`-fsanitize-blacklist`
  in the tree's own flags;
* no sanitizer suppression, ignorelist or blacklist file — nor a file named
  like one, such as `*.supp` or `asan*.txt` — that either build can reach, in
  the tree or generated beside it; the names `ASAN_OPTIONS`, `UBSAN_OPTIONS`,
  `LSAN_OPTIONS` and every other sanitizer's equivalent appear nowhere in the
  tree, a comment or a script included; and no name from the sanitizer run-time's own interface appears anywhere in
  the tree, the fuzzing harness and comments included — not `__lsan_disable`,
  `__lsan_ignore_object`, `__lsan_default_suppressions`,
  `__asan_default_options` or `__sanitizer_set_report_path`, nor any other name
  of the `__asan_`, `__lsan_`, `__ubsan_`, `__msan_`, `__tsan_`, `__hwasan_` or
  `__sanitizer_` family, nor `disable_sanitizer_instrumentation`;
* no `NOLINT` and no `OPENSSL_SUPPRESS_DEPRECATED`, comments included;
* nothing that stops a compile line being readable: the compiler every compile
  rule actually runs is the system one under `/usr`, nothing sets a compiler
  launcher in front of it — not the tree's CMake files, and not a fragment
  generated into the build directory and included from there — and there is no
  `-wrapper`, no `-specs=`, no `-fplugin=`, no `@file` and no `-include` or
  `-imacros` naming a program or a file outside `/usr`. A switch added by a
  wrapper script, or by a header pre-included ahead of every file, is the same
  switch; it is only harder to see.

A diagnostic is answered by changing the code it is about. An unused parameter
is named or marked; a comparison between signed and unsigned is fixed by
choosing the right type or saying which conversion is meant; a function called
without a prototype gets one, in the header that publishes it.

### Everything the tree answers, it answers clean under the sanitizers

Configured a third time, with `-fsanitize=address,undefined` and
`-fno-sanitize-recover=all` on the compile lines and the sanitizer on the link
lines, this tree gives every answer the ordinary build gives — the engine
through its C interface, the simulator through its TCP port, over the same
range of inputs, well-formed and malformed — and no report of any kind is
produced while it does. `ASAN_OPTIONS=detect_leaks=0` on that build: the
simulator is a server that is shut down rather than exited, so whether a leak
check runs at all would depend on how its last session ended. Leaks are asked
about in the fuzz run, which exits normally every time.

A report that is fixed by not getting there is not fixed. Under the sanitizers
this tree answers no worse than it answers without them, and a build that goes
quiet by answering fewer inputs fails this the same way a build that reports
does.

### A fuzzing harness, and a corpus for it

The tree ships at least one harness under `/app/fuzz/`:

* `fuzz/<name>.c` defines
  `int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)`. It may also
  define `int LLVMFuzzerInitialize(int *argc, char ***argv)`, called once
  before the first input. It defines no `main`; one is supplied.
* `fuzz/corpus/<name>/` holds that harness's seed inputs — at least 8 files,
  at least 6 of them with distinct contents, and not all of them empty. The
  count is per harness: two corpora of four files each are two corpora that
  are too small, not one that is big enough.

Every `.c` directly under `fuzz/` is a harness and is held to all of this. A
helper that does not define the entry point is not a harness and does not
belong there; put it in a subdirectory and include it.

Each harness is built against a driver of its own with the sanitizer flags
above, the engine's five include directories on the command line, and
`libCryptoEngine.so` and `libplatform.so` linked in. It is then given every
seed once in name order, followed by 2000 further inputs, each a mutation of a
seed or of an earlier mutant, from a pseudo-random generator whose seed is a
constant. It is a fixed number of inputs and not a time budget, so the run is
the same run every time; a defect it finds is a defect it finds again.

Three things are required of the harness beyond compiling:

* **it reaches the engine, in more than one place.** In line coverage over the
  instrumented build, the run executes at least 150 distinct lines
  under `src/OsslCryptoEngine`, spread over at least 7 of that directory's ten
  source files. Six of them are what `_cpri__InitCryptoUnits` reaches on its
  own, so the seventh is the first one that says anything about what the
  harness drove. A harness that parses the input into a shape of its own and
  never calls the tree does not satisfy this step, however well it is written;
  neither does one that calls a single entry point 2000 times.
* **the mutations reach it as well.** Counted in line executions rather than
  distinct lines, the whole run reaches the engine at least twice as often as
  a run of the seeds alone. A harness that accepts the seeds it was given and
  refuses every mutation of them has driven nothing with the 2000.
* **it finds nothing.** No sanitizer report and no non-zero exit over the whole
  run, with leak detection on. Whatever the bytes are taken to mean, the engine
  entry points it drives handle them: an input that asks for a 4 GB buffer, or
  names an algorithm that does not exist, or ends in the middle of a length
  field, produces a refusal rather than a crash.

Which parts of the engine the harness drives is your decision; that it drives
several is not. The engine's public surface is `CryptoEngine.h`; hashing,
HMAC, symmetric encryption, the big-number and marshalling helpers and the
key-import paths all take caller-supplied lengths and buffers, and all of them
are reachable from a byte string with a little structure imposed on it. Decide
what the first few bytes select, document it in the file, and seed the corpus
with inputs that exercise more than one branch of it.

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

This is the requirement most at risk at this step, and in a particular way.
Some of what the sanitizers report is code that computes the right answer by
accident. `x << 24` where `x` is a signed value with the top bit set is
undefined, and on this compiler it produces the byte pattern the author wanted;
so does the well-defined version. Others are not: a shift that is undefined
*and* wrong would already show as a changed answer. The fix for each of them keeps
the answer and removes the undefinedness. A fix that changes a digest has not
fixed anything.

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
meaning of every return value stay as they are. A prototype this step adds for
a function the engine already exports is not a change to that interface; it is
the interface finally saying what was always true.

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
that fence satisfied rather than switched off — no warning flag, diagnostic
pragma or language standard older than C99, no `#undef` or `#define` of either
fence macro anywhere in the tree, no `#define` of a retired name and no
prototype written for one — and no retired name called or imported; the four
process-global
randomness routes gone, both draws taking the engine's context and stating a
security strength, the generator named on that context by a call whose return
value is checked, and the platform's entropy reaching that generator as
additional input; every symmetric operation keyed with an `EVP_SKEY` built in
the engine's own context and released on every path out, with no key bytes
handed to a cipher context; and nothing this engine allocates from libcrypto's
allocator still live when it is done. A step that moves forward by undoing one
of those has not moved forward.

Two of them are where this step lands, so they are worth spelling out:

* **The 3.4 fence is still up, and still holds at 3.4.** Neither fence is
  baked into the project's own CMake files, and looking for the 3.4 macros in
  the tree will not find them: each fence is a pair of macros handed to a
  configure of its own, and both of those configures have to come out clean.
  This step adds the second and wider of them, at 3.5; it does not replace the
  first. A tree that satisfies the new fence by writing an API level into
  `add_compile_options` or into a target's compile options, where it lands
  after the macros the older configure passed, has switched the older fence
  off while leaving its flags exactly where they were.
* **The engine still allocates nothing it does not free.** The fuzz run puts a
  leak detector on a process that exits normally. A harness must not leak per
  input, and no engine entry point may leak when it refuses an input.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
