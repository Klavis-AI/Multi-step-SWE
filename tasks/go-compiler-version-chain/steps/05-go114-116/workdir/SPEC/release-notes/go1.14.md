# Go 1.14 Release Notes

## Introduction to Go 1.14

The latest Go release, version 1.14, arrives six months after Go 1.13.
Most of its changes are in the implementation of the toolchain, runtime, and libraries.
As always, the release maintains the Go 1 promise of compatibility.
We expect almost all Go programs to continue to compile and run as before.

Module support in the go command is now ready for production use,
and we encourage all users to migrate to Go
modules for dependency management. If you are unable to migrate due to a problem in the Go
toolchain, please ensure that the problem has an
open issue
filed. (If the issue is not on the Go1.15 milestone, please let us
know why it prevents you from migrating so that we can prioritize it
appropriately.)

## Changes to the language

Per the overlapping interfaces proposal,
Go 1.14 now permits embedding of interfaces with overlapping method sets:
methods from an embedded interface may have the same names and identical signatures
as methods already present in the (embedding) interface. This solves problems that typically
(but not exclusively) occur with diamond-shaped embedding graphs.
Explicitly declared methods in an interface must remain
unique, as before.

## Ports

### Darwin

Go 1.14 is the last release that will run on macOS 10.11 El Capitan.
Go 1.15 will require macOS 10.12 Sierra or later.

Go 1.14 is the last Go release to support 32-bit binaries on
macOS (the darwin/386 port). They are no longer
supported by macOS, starting with macOS 10.15 (Catalina).
Go continues to support the 64-bit darwin/amd64 port.

Go 1.14 will likely be the last Go release to support 32-bit
binaries on iOS, iPadOS, watchOS, and tvOS
(the darwin/arm port). Go continues to support the
64-bit darwin/arm64 port.

### Windows

Go binaries on Windows now
have DEP
(Data Execution Prevention) enabled.

On Windows, creating a file
via os.OpenFile with
the os.O_CREATE flag, or
via syscall.Open with
the syscall.O_CREAT
flag, will now create the file as read-only if the
bit 0o200 (owner write permission) is not set in the
permission argument. This makes the behavior on Windows more like
that on Unix systems.

### WebAssembly

JavaScript values referenced from Go via js.Value
objects can now be garbage collected.

js.Value values can no longer be compared using
the == operator, and instead must be compared using
their Equal method.

js.Value now
has IsUndefined, IsNull,
and IsNaN methods.

### RISC-V

Go 1.14 contains experimental support for 64-bit RISC-V on Linux
(GOOS=linux, GOARCH=riscv64). Be aware
that performance, assembly syntax stability, and possibly
correctness are a work in progress.

### FreeBSD

Go now supports the 64-bit ARM architecture on FreeBSD 12.0 or later (the
freebsd/arm64 port).

### Native Client (NaCl)

As announced in the Go 1.13 release notes,
Go 1.14 drops support for the Native Client platform (GOOS=nacl).

### Illumos

The runtime now respects zone CPU caps
(the zone.cpu-cap resource control)
for runtime.NumCPU and the default value
of GOMAXPROCS.

## Tools

### Go command

#### Vendoring

When the main module contains a top-level vendor directory and
its go.mod file specifies go 1.14 or
higher, the go command now defaults to -mod=vendor
for operations that accept that flag. A new value for that flag,
-mod=mod, causes the go command to instead load
modules from the module cache (as when no vendor directory is
present).

When -mod=vendor is set (explicitly or by default), the
go command now verifies that the main module’s
vendor/modules.txt file is consistent with its
go.mod file.

go list -m no longer silently omits
transitive dependencies that do not provide packages in
the vendor directory. It now fails explicitly if
-mod=vendor is set and information is requested for a module not
mentioned in vendor/modules.txt.

#### Flags

The go get command no longer accepts
the -mod flag. Previously, the flag’s setting either
was ignored or
caused the build to fail.

-mod=readonly is now set by default when the go.mod
file is read-only and no top-level vendor directory is present.

-modcacherw is a new flag that instructs the go
command to leave newly-created directories in the module cache at their
default permissions rather than making them read-only.
The use of this flag makes it more likely that tests or other tools will
accidentally add files not included in the module’s verified checksum.
However, it allows the use of rm -rf
(instead of go clean -modcache)
to remove the module cache.

-modfile=file is a new flag that instructs the go
command to read (and possibly write) an alternate go.mod file
instead of the one in the module root directory. A file
named go.mod must still be present in order to determine the
module root directory, but it is not accessed. When -modfile is
specified, an alternate go.sum file is also used: its path is
derived from the -modfile flag by trimming the .mod
extension and appending .sum.

#### Environment variables

GOINSECURE is a new environment variable that instructs
the go command to not require an HTTPS connection, and to skip
certificate validation, when fetching certain modules directly from their
origins. Like the existing GOPRIVATE variable, the value
of GOINSECURE is a comma-separated list of glob patterns.

#### Commands outside modules

When module-aware mode is enabled explicitly (by setting
GO111MODULE=on), most module commands have more
limited functionality if no go.mod file is present. For
example, go build,
go run, and other build commands can only build
packages in the standard library and packages specified as .go
files on the command line.

Previously, the go command would resolve each package path
to the latest version of a module but would not record the module path
or version. This resulted in slow,
non-reproducible builds.

go get continues to work as before, as do
go mod download and
go list -m with explicit versions.

#### +incompatible versions

If the latest version of a module contains a go.mod file,
go get will no longer upgrade to an
incompatible
major version of that module unless such a version is requested explicitly
or is already required.
go list also omits incompatible major versions
for such a module when fetching directly from version control, but may
include them if reported by a proxy.

#### go.mod file maintenance

go commands other than
go mod tidy no longer
remove a require directive that specifies a version of an indirect dependency
that is already implied by other (transitive) dependencies of the main
module.

go commands other than
go mod tidy no longer
edit the go.mod file if the changes are only cosmetic.

When -mod=readonly is set, go commands will no
longer fail due to a missing go directive or an erroneous
// indirect comment.

#### Module downloading

The go command now supports Subversion repositories in module mode.

The go command now includes snippets of plain-text error messages
from module proxies and other HTTP servers.
An error message will only be shown if it is valid UTF-8 and consists of only
graphic characters and spaces.

#### Testing

go test -v now streams t.Log output as it happens,
rather than at the end of all tests.

## Runtime

This release improves the performance of most uses
of defer to incur almost zero overhead compared to
calling the deferred function directly.
As a result, defer can now be used in
performance-critical code without overhead concerns.

Goroutines are now asynchronously preemptible.
As a result, loops without function calls no longer potentially
deadlock the scheduler or significantly delay garbage collection.
This is supported on all platforms except windows/arm,
darwin/arm, js/wasm, and
plan9/*.

A consequence of the implementation of preemption is that on Unix
systems, including Linux and macOS systems, programs built with Go
1.14 will receive more signals than programs built with earlier
releases.
This means that programs that use packages
like syscall
or golang.org/x/sys/unix
will see more slow system calls fail with EINTR errors.
Those programs will have to handle those errors in some way, most
likely looping to try the system call again. For more
information about this
see man 7 signal for Linux systems or similar documentation for
other systems.

The page allocator is more efficient and incurs significantly less
lock contention at high values of GOMAXPROCS.
This is most noticeable as lower latency and higher throughput for
large allocations being done in parallel and at a high rate.

Internal timers, used by
time.After,
time.Tick,
net.Conn.SetDeadline,
and friends, are more efficient, with less lock contention and fewer
context switches.
This is a performance improvement that should not cause any user
visible changes.

## Compiler

This release adds -d=checkptr as a compile-time option
for adding instrumentation to check that Go code is following
unsafe.Pointer safety rules dynamically.
This option is enabled by default (except on Windows) with
the -race or -msan flags, and can be
disabled with -gcflags=all=-d=checkptr=0.
Specifically, -d=checkptr checks the following:

- When converting unsafe.Pointer to *T,
the resulting pointer must be aligned appropriately
for T.

- If the result of pointer arithmetic points into a Go heap object,
one of the unsafe.Pointer-typed operands must point
into the same object.

Using -d=checkptr is not currently recommended on
Windows because it causes false alerts in the standard library.

The compiler can now emit machine-readable logs of key optimizations
using the -json flag, including inlining, escape
analysis, bounds-check elimination, and nil-check elimination.

Detailed escape analysis diagnostics (-m=2) now work again.
This had been dropped from the new escape analysis implementation in
the previous release.

All Go symbols in macOS binaries now begin with an underscore,
following platform conventions.

This release includes experimental support for compiler-inserted
coverage instrumentation for fuzzing.
See issue 14565 for more
details.
This API may change in future releases.

Bounds check elimination now uses information from slice creation and can
eliminate checks for indexes with types smaller than int.

## Standard library

### New byte sequence hashing package

Go 1.14 includes a new package,
hash/maphash,
which provides hash functions on byte sequences.
These hash functions are intended to be used to implement hash tables or
other data structures that need to map arbitrary strings or byte
sequences to a uniform distribution on unsigned 64-bit integers.

The hash functions are collision-resistant but not cryptographically secure.

The hash value of a given byte sequence is consistent within a
single process, but will be different in different processes.

### Minor changes to the library

As always, there are various minor changes and updates to the library,
made with the Go 1 promise of compatibility
in mind.

#### crypto/tls

Support for SSL version 3.0 (SSLv3) has been removed. Note that SSLv3 is the
cryptographically broken
protocol predating TLS.

TLS 1.3 can’t be disabled via the GODEBUG environment
variable anymore. Use the
Config.MaxVersion
field to configure TLS versions.

When multiple certificate chains are provided through the
Config.Certificates
field, the first one compatible with the peer is now automatically
selected. This allows for example providing an ECDSA and an RSA
certificate, and letting the package automatically select the best one.
Note that the performance of this selection is going to be poor unless the
Certificate.Leaf
field is set. The
Config.NameToCertificate
field, which only supports associating a single certificate with
a give name, is now deprecated and should be left as nil.
Similarly the
Config.BuildNameToCertificate
method, which builds the NameToCertificate field
from the leaf certificates, is now deprecated and should not be
called.

The new CipherSuites
and InsecureCipherSuites
functions return a list of currently implemented cipher suites.
The new CipherSuiteName
function returns a name for a cipher suite ID.

The new 
(*ClientHelloInfo).SupportsCertificate and

(*CertificateRequestInfo).SupportsCertificate
methods expose whether a peer supports a certain certificate.

The tls package no longer supports the legacy Next Protocol
Negotiation (NPN) extension and now only supports ALPN. In previous
releases it supported both. There are no API changes and applications
should function identically as before. Most other clients and servers have
already removed NPN support in favor of the standardized ALPN.

RSA-PSS signatures are now used when supported in TLS 1.2 handshakes. This
won’t affect most applications, but custom
Certificate.PrivateKey
implementations that don’t support RSA-PSS signatures will need to use the new

Certificate.SupportedSignatureAlgorithms
field to disable them.

Config.Certificates and
Config.GetCertificate
can now both be nil if
Config.GetConfigForClient
is set. If the callbacks return neither certificates nor an error, the
unrecognized_name is now sent.

The new CertificateRequestInfo.Version
field provides the TLS version to client certificates callbacks.

The new TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 and
TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 constants use
the final names for the cipher suites previously referred to as
TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305 and
TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305.

#### crypto/x509

Certificate.CreateCRL
now supports Ed25519 issuers.

#### debug/dwarf

The debug/dwarf package now supports reading DWARF
version 5.

The new
method (*Data).AddSection
supports adding arbitrary new DWARF sections from the input file
to the DWARF Data.

The new
method (*Reader).ByteOrder
returns the byte order of the current compilation unit.
This may be used to interpret attributes that are encoded in the
native ordering, such as location descriptions.

The new
method (*LineReader).Files
returns the file name table from a line reader.
This may be used to interpret the value of DWARF attributes such
as AttrDeclFile.

#### encoding/asn1

Unmarshal
now supports ASN.1 string type BMPString, represented by the new
TagBMPString
constant.

#### encoding/json

The Decoder
type supports a new
method InputOffset
that returns the input stream byte offset of the current
decoder position.

Compact no longer
escapes the U+2028 and U+2029 characters, which
was never a documented feature. For proper escaping, see HTMLEscape.

Number no longer
accepts invalid numbers, to follow the documented behavior more closely.
If a program needs to accept invalid numbers like the empty string,
consider wrapping the type with Unmarshaler.

Unmarshal
can now support map keys with string underlying type which implement
encoding.TextUnmarshaler.

#### go/build

The Context
type has a new field Dir which may be used to set
the working directory for the build.
The default is the current directory of the running process.
In module mode, this is used to locate the main module.

#### go/doc

The new
function NewFromFiles
computes package documentation from a list
of *ast.File’s and associates examples with the
appropriate package elements.
The new information is available in a new Examples
field
in the Package, Type,
and Func types, and a
new Suffix
field in
the Example
type.

#### io/ioutil

TempDir can now create directories
whose names have predictable prefixes and suffixes.
As with TempFile, if the pattern
contains a ‘*’, the random string replaces the last ‘*’.

#### log

The
new Lmsgprefix
flag may be used to tell the logging functions to emit the
optional output prefix immediately before the log message rather
than at the start of the line.

#### math

The new FMA function
computes x*y+z in floating point with no
intermediate rounding of the x*y
computation. Several architectures implement this computation
using dedicated hardware instructions for additional performance.

#### math/big

The GCD method
now allows the inputs a and b to be
zero or negative.

#### math/bits

The new functions
Rem,
Rem32, and
Rem64
support computing a remainder even when the quotient overflows.

#### mime

The default type of .js and .mjs files
is now text/javascript rather
than application/javascript.
This is in accordance
with an
IETF draft that treats application/javascript as obsolete.

#### mime/multipart

The
new Reader
method NextRawPart
supports fetching the next MIME part without transparently
decoding quoted-printable data.

#### net/http

The new Header
method Values
can be used to fetch all values associated with a
canonicalized key.

The
new Transport
field DialTLSContext
can be used to specify an optional dial function for creating
TLS connections for non-proxied HTTPS requests.
This new field can be used instead
of DialTLS,
which is now considered deprecated; DialTLS will
continue to work, but new code should
use DialTLSContext, which allows the transport to
cancel dials as soon as they are no longer needed.

On Windows, ServeFile now correctly
serves files larger than 2GB.

#### net/http/httptest

The
new Server
field EnableHTTP2
supports enabling HTTP/2 on the test server.

#### net/textproto

The
new MIMEHeader
method Values
can be used to fetch all values associated with a canonicalized
key.

#### net/url

When parsing of a URL fails
(for example by Parse
or ParseRequestURI),
the resulting Error message
will now quote the unparsable URL.
This provides clearer structure and consistency with other parsing errors.

#### os/signal

On Windows,
the CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT,
and CTRL_SHUTDOWN_EVENT events now generate
a syscall.SIGTERM signal, similar to how Control-C
and Control-Break generate a syscall.SIGINT signal.

#### plugin

The plugin package now supports freebsd/amd64.

#### reflect

StructOf now
supports creating struct types with unexported fields, by
setting the PkgPath field in
a StructField element.

#### runtime

runtime.Goexit can no longer be aborted by a
recursive panic/recover.

On macOS, SIGPIPE is no longer forwarded to signal
handlers installed before the Go runtime is initialized.
This is necessary because macOS delivers SIGPIPE
to the main thread
rather than the thread writing to the closed pipe.

#### runtime/pprof

The generated profile no longer includes the pseudo-PCs used for inline
marks. Symbol information of inlined functions is encoded in
the format
the pprof tool expects. This is a fix for the regression introduced
during recent releases.

#### strconv

The NumError
type now has
an Unwrap
method that may be used to retrieve the reason that a conversion
failed.
This supports using NumError values
with errors.Is to see
if the underlying error
is strconv.ErrRange
or strconv.ErrSyntax.

#### sync

Unlocking a highly contended Mutex now directly
yields the CPU to the next goroutine waiting for
that Mutex. This significantly improves the
performance of highly contended mutexes on high CPU count
machines.

#### testing

The testing package now supports cleanup functions, called after
a test or benchmark has finished, by calling
T.Cleanup or
B.Cleanup respectively.

#### text/template

The text/template package now correctly reports errors when a
parenthesized argument is used as a function.
This most commonly shows up in erroneous cases like
{{if (eq .F "a") or (eq .F "b")}}.
This should be written as {{if or (eq .F "a") (eq .F "b")}}.
The erroneous case never worked as expected, and will now be
reported with an error can't give argument to non-function.

JSEscape now
escapes the & and = characters to
mitigate the impact of its output being misused in HTML contexts.

#### unicode

The unicode package and associated
support throughout the system has been upgraded from Unicode 11.0 to
Unicode 12.0,
which adds 554 new characters, including four new scripts, and 61 new emoji.
