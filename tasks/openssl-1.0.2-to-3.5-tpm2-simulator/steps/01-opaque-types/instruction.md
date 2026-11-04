# Step 1 — build the crypto engine against the system OpenSSL 1.1.0

## 1. What this step is

`/app` is the TPM 2.0 reference simulator. Its crypto engine
(`src/OsslCryptoEngine/`) does not use an installed OpenSSL: it carries its own
copy of OpenSSL 1.0.2b inside the repository — the headers under `lib/openssl/`
and the static archives under `lib/arm/`, `lib/x86/`, `lib/x86-64/` and
`lib/libeay32.lib` — and links it in statically. Those headers publish the
layout of the types the engine works with, so the engine stores OpenSSL
structures inside its own structures by value, reads and writes their fields
directly, and includes an OpenSSL-internal header that no OpenSSL release
installs.

OpenSSL 1.1.0 stopped publishing the layout of many of them: those definitions
moved out of the public headers, and the fields are reached through accessor
functions instead. Which types that happened to is something the installed
headers themselves say. Nothing in the engine's own API changes, and nothing it
computes changes.

This machine has OpenSSL 1.1.0l installed as the system OpenSSL
(`openssl version`). 1.0.2 is not installed and is not available.

Move the engine onto the installed 1.1.0l, and take the vendored copy out of the
repository.

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
build directory is not part of the repository and its name is not fixed: any
directory outside the source tree is one, and the build still produces all four
of `platform/libplatform.so`, `OsslCryptoEngine/libCryptoEngine.so`,
`tpm/libTPM.so` and `Simulator` in it.

The tree stays an ordinary CMake project while it does so, and CMake's own
options keep their ordinary effect on it: configuring that same build directory
again with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` added writes a
`compile_commands.json` describing the translation units this tree compiles and
the flags it compiles them with.

**OpenSSL comes from the system.** Every object the build produces resolves its
OpenSSL symbols through the dynamic loader at run time and defines none of them
itself: `libCryptoEngine.so` links the installed `libcrypto`, and none of it,
`libplatform.so`, `libTPM.so` or `Simulator` carries OpenSSL's own code inside
it.

**The vendored copy is gone.** No OpenSSL header and no OpenSSL static archive
remains anywhere under `/app` — not unreferenced, not unused, not moved aside.
The repository stops containing an OpenSSL.

**No reaching inside OpenSSL's types.** In the sources the build compiles:

* no expression reads or writes a field of a structure or union that OpenSSL
  declares, whether directly or through a pointer;
* no variable, structure field or function parameter holds, *by value*, a
  structure or union whose definition the installed OpenSSL no longer
  publishes. Where the engine needs one of those, it holds what the library
  gives it: a pointer obtained from the library.

**Digest contexts have a lifecycle.** The engine obtains every `EVP_MD_CTX` from
libcrypto's own allocator and gives it back to libcrypto, rather than declaring
one in its own storage and initialising it in place.

**libcrypto is initialised explicitly.** `_cpri__InitCryptoUnits`, the engine's
own startup entry point, initialises libcrypto itself rather than leaving it to
whatever the library does on first use.

**The engine's C interface is unchanged.** A consumer outside the tree that
includes `CryptoEngine.h` and links `-lCryptoEngine -lplatform` — with the
engine's headers found under `src/include`, `src/tpm/include`,
`src/tpm/include/prototypes`, `src/platform/include` and
`src/platform/include/prototypes`, as they are now — still compiles, links and
runs. Function signatures, the types they take, the constants they use and the
meaning of every return value stay as they are. This is a migration of how the
engine is written, not of what it offers.

**The simulator is started the same way.** `Simulator <port>` serves the TCP
port named on its command line, and the platform signal channel is the next port
above the one being served. Whatever starts the simulator today reaches the same
simulator afterwards.

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

Two consequences of that are worth stating outright, because they are properties
of this tree rather than of the port:

* **State the layers above copy must survive being copied.** Anything the engine
  leaves in a structure that the rest of the TPM relocates, duplicates or writes
  out as bytes — and it does relocate, duplicate and write out the structures
  the engine hands it — must still be valid and still mean the same thing in
  the copy as it does in the original.
* **Behaviour that used to be free is not automatically free.** 1.1.0 validates
  in places where 1.0.2 accepted whatever it was given, and the tree's answers
  still have to come out the same.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in `/app`. The result is the repository, migrated in place: keep its
  layout, its build system and its sources where they are.
- Do not cheat by using online solutions or hints specific to this task.
