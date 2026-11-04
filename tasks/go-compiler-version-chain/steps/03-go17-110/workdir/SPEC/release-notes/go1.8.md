# Go 1.8 Release Notes

## Introduction to Go 1.8

The latest Go release, version 1.8, arrives six months after Go 1.7.
Most of its changes are in the implementation of the toolchain, runtime, and libraries.
There are two minor changes to the language specification.
As always, the release maintains the Go 1 promise of compatibility.
We expect almost all Go programs to continue to compile and run as before.

The release adds support for 32-bit MIPS,
updates the compiler back end to generate more efficient code,
reduces GC pauses by eliminating stop-the-world stack rescanning,
adds HTTP/2 Push support,
adds HTTP graceful shutdown,
adds more context support,
enables profiling mutexes,
and simplifies sorting slices.

## Changes to the language

When explicitly converting a value from one struct type to another,
as of Go 1.8 the tags are ignored. Thus two structs that differ
only in their tags may be converted from one to the other:

```
func example() {
    type T1 struct {
        X int `json:"foo"`
    }
    type T2 struct {
        X int `json:"bar"`
    }
    var v1 T1
    var v2 T2
    v1 = T1(v2) // now legal
}

The language specification now only requires that implementations
support up to 16-bit exponents in floating-point constants. This does not affect
either the “gc” or
gccgo compilers, both of
which still support 32-bit exponents.

## Ports

Go now supports 32-bit MIPS on Linux for both big-endian
(linux/mips) and little-endian machines
(linux/mipsle) that implement the MIPS32r1 instruction set with FPU
or kernel FPU emulation. Note that many common MIPS-based routers lack an FPU and
have firmware that doesn’t enable kernel FPU emulation; Go won’t run on such machines.

On DragonFly BSD, Go now requires DragonFly 4.4.4 or later.

On OpenBSD, Go now requires OpenBSD 5.9 or later.

The Plan 9 port’s networking support is now much more complete
and matches the behavior of Unix and Windows with respect to deadlines
and cancellation. For Plan 9 kernel requirements, see the
Plan 9 wiki page.

Go 1.8 now only supports OS X 10.8 or later. This is likely the last
Go release to support 10.8. Compiling Go or running
binaries on older OS X versions is untested.

Go 1.8 will be the last release to support Linux on ARMv5E and ARMv6 processors:
Go 1.9 will likely require the ARMv6K (as found in the Raspberry Pi 1) or later.
To identify whether a Linux system is ARMv6K or later, run
“go tool dist -check-armv6k”
(to facilitate testing, it is also possible to just copy the dist command to the
system without installing a full copy of Go 1.8)
and if the program terminates with output “ARMv6K supported.” then the system
implements ARMv6K or later.
Go on non-Linux ARM systems already requires ARMv6K or later.

zos is now a recognized value for GOOS,
reserved for the z/OS operating system.

### Known Issues

There are some instabilities on FreeBSD and NetBSD that are known but not understood.
These can lead to program crashes in rare cases.
See
issue 15658 and
issue 16511.
Any help in solving these issues would be appreciated.

## Tools

### Assembler

For 64-bit x86 systems, the following instructions have been added:
VBROADCASTSD,
BROADCASTSS,
MOVDDUP,
MOVSHDUP,
MOVSLDUP,
VMOVDDUP,
VMOVSHDUP, and
VMOVSLDUP.

For 64-bit PPC systems, the common vector scalar instructions have been
added:
LXS,
LXSDX,
LXSI,
LXSIWAX,
LXSIWZX,
LXV,
LXVD2X,
LXVDSX,
LXVW4X,
MFVSR,
MFVSRD,
MFVSRWZ,
MTVSR,
MTVSRD,
MTVSRWA,
MTVSRWZ,
STXS,
STXSDX,
STXSI,
STXSIWX,
STXV,
STXVD2X,
STXVW4X,
XSCV,
XSCVDPSP,
XSCVDPSPN,
XSCVDPSXDS,
XSCVDPSXWS,
XSCVDPUXDS,
XSCVDPUXWS,
XSCVSPDP,
XSCVSPDPN,
XSCVSXDDP,
XSCVSXDSP,
XSCVUXDDP,
XSCVUXDSP,
XSCVX,
XSCVXP,
XVCV,
XVCVDPSP,
XVCVDPSXDS,
XVCVDPSXWS,
XVCVDPUXDS,
XVCVDPUXWS,
XVCVSPDP,
XVCVSPSXDS,
XVCVSPSXWS,
XVCVSPUXDS,
XVCVSPUXWS,
XVCVSXDDP,
XVCVSXDSP,
XVCVSXWDP,
XVCVSXWSP,
XVCVUXDDP,
XVCVUXDSP,
XVCVUXWDP,
XVCVUXWSP,
XVCVX,
XVCVXP,
XXLAND,
XXLANDC,
XXLANDQ,
XXLEQV,
XXLNAND,
XXLNOR,
XXLOR,
XXLORC,
XXLORQ,
XXLXOR,
XXMRG,
XXMRGHW,
XXMRGLW,
XXPERM,
XXPERMDI,
XXSEL,
XXSI,
XXSLDWI,
XXSPLT, and
XXSPLTW.

### Yacc

The yacc tool (previously available by running
“go tool yacc”) has been removed.
As of Go 1.7 it was no longer used by the Go compiler.
It has moved to the “tools” repository and is now available at
golang.org/x/tools/cmd/goyacc.

### Fix

The fix tool has a new “context”
fix to change imports from “golang.org/x/net/context”
to “context”.

### Pprof

The pprof tool can now profile TLS servers
and skip certificate validation by using the “https+insecure”
URL scheme.

The callgrind output now has instruction-level granularity.

### Trace

The trace tool has a new -pprof flag for
producing pprof-compatible blocking and latency profiles from an
execution trace.

Garbage collection events are now shown more clearly in the
execution trace viewer. Garbage collection activity is shown on its
own row and GC helper goroutines are annotated with their roles.

### Vet

Vet is stricter in some ways and looser where it
previously caused false positives.

Vet now checks for copying an array of locks,
duplicate JSON and XML struct field tags,
non-space-separated struct tags,
deferred calls to HTTP Response.Body.Close
before checking errors, and
indexed arguments in Printf.
It also improves existing checks.

### Compiler Toolchain

Go 1.7 introduced a new compiler back end for 64-bit x86 systems.
In Go 1.8, that back end has been developed further and is now used for
all architectures.

The new back end, based on
static single assignment form (SSA),
generates more compact, more efficient code
and provides a better platform for optimizations
such as bounds check elimination.
The new back end reduces the CPU time required by
our benchmark programs by 20-30%
on 32-bit ARM systems. For 64-bit x86 systems, which already used the SSA back end in
Go 1.7, the gains are a more modest 0-10%. Other architectures will likely
see improvements closer to the 32-bit ARM numbers.

The temporary -ssa=0 compiler flag introduced in Go 1.7
to disable the new back end has been removed in Go 1.8.

In addition to enabling the new compiler back end for all systems,
Go 1.8 also introduces a new compiler front end. The new compiler
front end should not be noticeable to users but is the foundation for
future performance work.

The compiler and linker have been optimized and run faster in this
release than in Go 1.7, although they are still slower than we would
like and will continue to be optimized in future releases.
Compared to the previous release, Go 1.8 is
about 15% faster.

### Cgo

The Go tool now remembers the value of the CGO_ENABLED environment
variable set during make.bash and applies it to all future compilations
by default to fix issue #12808.
When doing native compilation, it is rarely necessary to explicitly set
the CGO_ENABLED environment variable as make.bash
will detect the correct setting automatically. The main reason to explicitly
set the CGO_ENABLED environment variable is when your environment
supports cgo, but you explicitly do not want cgo support, in which case, set
CGO_ENABLED=0 during make.bash or all.bash.

The environment variable PKG_CONFIG may now be used to
set the program to run to handle #cgo pkg-config
directives. The default is pkg-config, the program
always used by earlier releases. This is intended to make it easier
to cross-compile
cgo code.

The cgo tool now supports a -srcdir
option, which is used by the go command.

If cgo code calls C.malloc, and
malloc returns NULL, the program will now
crash with an out of memory error.
C.malloc will never return nil.
Unlike most C functions, C.malloc may not be used in a
two-result form returning an errno value.

If cgo is used to call a C function passing a
pointer to a C union, and if the C union can contain any pointer
values, and if cgo pointer
checking is enabled (as it is by default), the union value is now
checked for Go pointers.

### Gccgo

Due to the alignment of Go’s semiannual release schedule with GCC’s
annual release schedule,
GCC release 6 contains the Go 1.6.1 version of gccgo.
We expect that the next release, GCC 7, will contain the Go 1.8
version of gccgo.

### Default GOPATH

The
GOPATH
environment variable now has a default value if it
is unset. It defaults to
$HOME/go on Unix and
%USERPROFILE%/go on Windows.

### Go get

The “go get” command now always respects
HTTP proxy environment variables, regardless of whether
the -insecure flag is used. In previous releases, the
-insecure flag had the side effect of not using proxies.

### Go bug

The new
“go bug”
command starts a bug report on GitHub, prefilled
with information about the current system.

### Go doc

The
“go doc”
command now groups constants and variables with their type,
following the behavior of
godoc.

In order to improve the readability of doc’s
output, each summary of the first-level items is guaranteed to
occupy a single line.

Documentation for a specific method in an interface definition can
now be requested, as in
“go doc net.Conn.SetDeadline”.

### Plugins

Go now provides early support for plugins with a “plugin”
build mode for generating plugins written in Go, and a
new plugin package for
loading such plugins at run time. Plugin support is currently only
available on Linux. Please report any issues.

## Runtime

### Argument Liveness

The garbage collector no longer considers
arguments live throughout the entirety of a function. For more
information, and for how to force a variable to remain live, see
the runtime.KeepAlive
function added in Go 1.7.

Updating:
Code that sets a finalizer on an allocated object may need to add
calls to runtime.KeepAlive in functions or methods
using that object.
Read the
KeepAlive
documentation and its example for more details.

### Concurrent Map Misuse

In Go 1.6, the runtime
added lightweight,
best-effort detection of concurrent misuse of maps. This release
improves that detector with support for detecting programs that
concurrently write to and iterate over a map.

As always, if one goroutine is writing to a map, no other goroutine should be
reading (which includes iterating) or writing the map concurrently.
If the runtime detects this condition, it prints a diagnosis and crashes the program.
The best way to find out more about the problem is to run the program
under the
race detector,
which will more reliably identify the race
and give more detail.

### MemStats Documentation

The runtime.MemStats
type has been more thoroughly documented.

## Performance

As always, the changes are so general and varied that precise statements
about performance are difficult to make.
Most programs should run a bit faster,
due to speedups in the garbage collector and
optimizations in the standard library.

There have been optimizations to implementations in the
bytes,
crypto/aes,
crypto/cipher,
crypto/elliptic,
crypto/sha256,
crypto/sha512,
encoding/asn1,
encoding/csv,
encoding/hex,
encoding/json,
hash/crc32,
image/color,
image/draw,
math,
math/big,
reflect,
regexp,
runtime,
strconv,
strings,
syscall,
text/template, and
unicode/utf8
packages.

### Garbage Collector

Garbage collection pauses should be significantly shorter than they
were in Go 1.7, usually under 100 microseconds and often as low as
10 microseconds.
See the
document on eliminating stop-the-world stack re-scanning
for details. More work remains for Go 1.9.

### Defer

The overhead of deferred
function calls has been reduced by about half.

### Cgo

The overhead of calls from Go into C has been reduced by about half.

## Standard library

### Examples

Examples have been added to the documentation across many packages.

### Sort

The sort package
now includes a convenience function
Slice to sort a
slice given a less function.
In many cases this means that writing a new sorter type is not
necessary.

Also new are
SliceStable and
SliceIsSorted.

### HTTP/2 Push

The net/http package now includes a
mechanism to
send HTTP/2 server pushes from a
Handler.
Similar to the existing Flusher and Hijacker
interfaces, an HTTP/2
ResponseWriter
now implements the new
Pusher interface.

### HTTP Server Graceful Shutdown

The HTTP Server now has support for graceful shutdown using the new
Server.Shutdown
method and abrupt shutdown using the new
Server.Close
method.

### More Context Support

Continuing Go 1.7’s adoption
of context.Context
into the standard library, Go 1.8 adds more context support
to existing packages:

  - The new Server.Shutdown
takes a context argument.

  - There have been significant additions to the
database/sql package with context support.

  - All nine of the new Lookup methods on the new
net.Resolver now
take a context.

### Mutex Contention Profiling

The runtime and tools now support profiling contended mutexes.

Most users will want to use the new -mutexprofile
flag with “go test”,
and then use pprof on the resultant file.

Lower-level support is also available via the new
MutexProfile
and
SetMutexProfileFraction.

A known limitation for Go 1.8 is that the profile only reports contention for
sync.Mutex,
not
sync.RWMutex.

### Minor changes to the library

As always, there are various minor changes and updates to the library,
made with the Go 1 promise of compatibility
in mind. The following sections list the user visible changes and additions.
Optimizations and minor bug fixes are not listed.

#### archive/tar

The tar implementation corrects many bugs in corner cases of the file format.
The Reader
is now able to process tar files in the PAX format with entries larger than 8GB.
The Writer
no longer produces invalid tar files in some situations involving long pathnames.

#### compress/flate

There have been some minor fixes to the encoder to improve the
compression ratio in certain situations. As a result, the exact
encoded output of DEFLATE may be different from Go 1.7. Since
DEFLATE is the underlying compression of gzip, png, zlib, and zip,
those formats may have changed outputs.

The encoder, when operating in
NoCompression
mode, now produces a consistent output that is not dependent on
the size of the slices passed to the
Write
method.

The decoder, upon encountering an error, now returns any
buffered data it had uncompressed along with the error.

#### compress/gzip

The Writer
now encodes a zero MTIME field when
the Header.ModTime
field is the zero value.
In previous releases of Go, the Writer would encode
a nonsensical value.
Similarly,
the Reader
now reports a zero encoded MTIME field as a zero
Header.ModTime.

#### context

The DeadlineExceeded
error now implements
net.Error
and reports true for both the Timeout and
Temporary methods.

#### crypto/tls

The new method
Conn.CloseWrite
allows TLS connections to be half closed.

The new method
Config.Clone
clones a TLS configuration.

The new Config.GetConfigForClient
callback allows selecting a configuration for a client dynamically, based
on the client’s
ClientHelloInfo.
The ClientHelloInfo
struct now has new
fields Conn, SignatureSchemes (using
the new
type SignatureScheme),
SupportedProtos, and SupportedVersions.

The new Config.GetClientCertificate
callback allows selecting a client certificate based on the server’s
TLS CertificateRequest message, represented by the new
CertificateRequestInfo.

The new
Config.KeyLogWriter
allows debugging TLS connections
in WireShark and
similar tools.

The new
Config.VerifyPeerCertificate
callback allows additional validation of a peer’s presented certificate.

The crypto/tls package now implements basic
countermeasures against CBC padding oracles. There should be
no explicit secret-dependent timings, but it does not attempt to
normalize memory accesses to prevent cache timing leaks.

The crypto/tls package now supports
X25519 and
ChaCha20-Poly1305.
ChaCha20-Poly1305 is now prioritized unless
hardware support for AES-GCM is present.

AES-128-CBC cipher suites with SHA-256 are also
now supported, but disabled by default.

#### crypto/x509

PSS signatures are now supported.

UnknownAuthorityError
now has a Cert field, reporting the untrusted
certificate.

Certificate validation is more permissive in a few cases and
stricter in a few other cases.

Root certificates will now also be looked for
at /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem
on Linux, to support RHEL and CentOS.

#### database/sql

The package now supports context.Context. There are new methods
ending in Context such as
DB.QueryContext and
DB.PrepareContext
that take context arguments. Using the new Context methods ensures that
connections are closed and returned to the connection pool when the
request is done; enables canceling in-progress queries
should the driver support that; and allows the database
pool to cancel waiting for the next available connection.

The IsolationLevel
can now be set when starting a transaction by setting the isolation level
on TxOptions.Isolation and passing
it to DB.BeginTx.
An error will be returned if an isolation level is selected that the driver
does not support. A read-only attribute may also be set on the transaction
by setting TxOptions.ReadOnly
to true.

Queries now expose the SQL column type information for drivers that support it.
Rows can return ColumnTypes
which can include SQL type information, column type lengths, and the Go type.

A Rows
can now represent multiple result sets. After
Rows.Next returns false,
Rows.NextResultSet
may be called to advance to the next result set. The existing Rows
should continue to be used after it advances to the next result set.

NamedArg may be used
as query arguments. The new function Named
helps create a NamedArg
more succinctly.

If a driver supports the new
Pinger
interface, the
DB.Ping
and
DB.PingContext
methods will use that interface to check whether a
database connection is still valid.

The new Context query methods work for all drivers, but
Context cancellation is not responsive unless the driver has been
updated to use them. The other features require driver support in
database/sql/driver.
Driver authors should review the new interfaces. Users of existing
driver should review the driver documentation to see what
it supports and any system specific documentation on each feature.

#### debug/pe

The package has been extended and is now used by
the Go linker to read gcc-generated object files.
The new
File.StringTable
and
Section.Relocs
fields provide access to the COFF string table and COFF relocations.
The new
File.COFFSymbols
allows low-level access to the COFF symbol table.

#### encoding/base64

The new
Encoding.Strict
method returns an Encoding that causes the decoder
to return an error when the trailing padding bits are not zero.

#### encoding/binary

Read
and
Write
now support booleans.

#### encoding/json

UnmarshalTypeError
now includes the struct and field name.

A nil Marshaler
now marshals as a JSON null value.

A RawMessage value now
marshals the same as its pointer type.

Marshal
encodes floating-point numbers using the same format as in ES6,
preferring decimal (not exponential) notation for a wider range of values.
In particular, all floating-point integers up to 264 format the
same as the equivalent int64 representation.

In previous versions of Go, unmarshaling a JSON null into an
Unmarshaler
was considered a no-op; now the Unmarshaler’s
UnmarshalJSON method is called with the JSON literal
null and can define the semantics of that case.

#### encoding/pem

Decode
is now strict about the format of the ending line.

#### encoding/xml

Unmarshal
now has wildcard support for collecting all attributes using
the new ",any,attr" struct tag.

#### expvar

The new methods
Int.Value,
String.Value,
Float.Value, and
Func.Value
report the current value of an exported variable.

The new
function Handler
returns the package’s HTTP handler, to enable installing it in
non-standard locations.

#### fmt

Scanf,
Fscanf, and
Sscanf now
handle spaces differently and more consistently than
previous releases. See the
scanning documentation
for details.

#### go/doc

The new IsPredeclared
function reports whether a string is a predeclared identifier.

#### go/types

The new function
Default
returns the default “typed” type for an “untyped” type.

The alignment of complex64 now matches
the Go compiler.

#### html/template

The package now validates
the "type" attribute on
a <script> tag.

#### image/png

Decode
(and DecodeConfig)
now supports True Color and grayscale transparency.

Encoder
is now faster and creates smaller output
when encoding paletted images.

#### math/big

The new method
Int.Sqrt
calculates ⌊√x⌋.

The new method
Float.Scan
is a support routine for
fmt.Scanner.

Int.ModInverse
now supports negative numbers.

#### math/rand

The new Rand.Uint64
method returns uint64 values. The
new Source64
interface describes sources capable of generating such values
directly; otherwise the Rand.Uint64 method
constructs a uint64 from two calls
to Source’s
Int63 method.

#### mime

ParseMediaType
now preserves unnecessary backslash escapes as literals,
in order to support MSIE.
When MSIE sends a full file path (in “intranet mode”), it does not
escape backslashes: “C:\dev\go\foo.txt”, not
“C:\\dev\\go\\foo.txt”.
If we see an unnecessary backslash escape, we now assume it is from MSIE
and intended as a literal backslash.
No known MIME generators emit unnecessary backslash escapes
for simple token characters like numbers and letters.

#### mime/quotedprintable

The
Reader’s
parsing has been relaxed in two ways to accept
more input seen in the wild.
First, it accepts an equals sign (=) not followed
by two hex digits as a literal equal sign.
Second, it silently ignores a trailing equals sign at the end of
an encoded input.

#### net

The Conn documentation
has been updated to clarify expectations of an interface
implementation. Updates in the net/http packages
depend on implementations obeying the documentation.

Updating: implementations of the Conn interface should verify
they implement the documented semantics. The
golang.org/x/net/nettest
package will exercise a Conn and validate it behaves properly.

The new method
UnixListener.SetUnlinkOnClose
sets whether the underlying socket file should be removed from the file system when
the listener is closed.

The new Buffers type permits
writing to the network more efficiently from multiple discontiguous buffers
in memory. On certain machines, for certain types of connections,
this is optimized into an OS-specific batch write operation (such as writev).

The new Resolver looks up names and numbers
and supports context.Context.
The Dialer now has an optional
Resolver field.

Interfaces is now supported on Solaris.

The Go DNS resolver now supports resolv.conf’s “rotate”
and “option ndots:0” options. The “ndots” option is
now respected in the same way as libresolve.

#### net/http

Server changes:

  - The server now supports graceful shutdown support, mentioned above.

  -

The Server
adds configuration options
ReadHeaderTimeout and IdleTimeout
and documents WriteTimeout.

  -

FileServer
and
ServeContent
now support HTTP If-Match conditional requests,
in addition to the previous If-None-Match
support for ETags properly formatted according to RFC 7232, section 2.3.

There are several additions to what a server’s Handler can do:

  -

The Context
returned
by Request.Context
is canceled if the underlying net.Conn
closes. For instance, if the user closes their browser in the
middle of a slow request, the Handler can now
detect that the user is gone. This complements the
existing CloseNotifier
support. This functionality requires that the underlying
net.Conn implements
recently clarified interface documentation.

  -

To serve trailers produced after the header has already been written,
see the new
TrailerPrefix
mechanism.

  -

A Handler can now abort a response by panicking
with the error
ErrAbortHandler.

  -

A Write of zero bytes to a
ResponseWriter
is now defined as a
way to test whether a ResponseWriter has been hijacked:
if so, the Write returns
ErrHijacked
without printing an error
to the server’s error log.

Client & Transport changes:

  -

The Client
now copies most request headers on redirect. See
the documentation
on the Client type for details.

  -

The Transport
now supports international domain names. Consequently, so do
Get and other helpers.

  -

The Client now supports 301, 307, and 308 redirects.
For example, Client.Post now follows 301
redirects, converting them to GET requests
without bodies, like it did for 302 and 303 redirect responses
previously.
The Client now also follows 307 and 308
redirects, preserving the original request method and body, if
any. If the redirect requires resending the request body, the
request must have the new
Request.GetBody
field defined.
NewRequest
sets Request.GetBody automatically for common
body types.

  -

The Transport now rejects requests for URLs with
ports containing non-digit characters.

  -

The Transport will now retry non-idempotent
requests if no bytes were written before a network failure
and the request has no body.

  -

The
new Transport.ProxyConnectHeader
allows configuration of header values to send to a proxy
during a CONNECT request.

  -

The DefaultTransport.Dialer
now enables DualStack ("Happy Eyeballs") support,
allowing the use of IPv4 as a backup if it looks like IPv6 might be
failing.

  -

The Transport
no longer reads a byte of a non-nil
Request.Body
when the
Request.ContentLength
is zero to determine whether the ContentLength
is actually zero or just undefined.
To explicitly signal that a body has zero length,
either set it to nil, or set it to the new value
NoBody.
The new NoBody value is intended for use by Request
constructor functions; it is used by
NewRequest.

#### net/http/httptrace

There is now support for tracing a client request’s TLS handshakes with
the new
ClientTrace.TLSHandshakeStart
and
ClientTrace.TLSHandshakeDone.

#### net/http/httputil

The ReverseProxy
has a new optional hook,
ModifyResponse,
for modifying the response from the back end before proxying it to the client.

#### net/mail

Empty quoted strings are once again allowed in the name part of
an address. That is, Go 1.4 and earlier accepted
"" <gopher@example.com>,
but Go 1.5 introduced a bug that rejected this address.
The address is recognized again.

The
Header.Date
method has always provided a way to parse
the Date: header.
A new function
ParseDate
allows parsing dates found in other
header lines, such as the Resent-Date: header.

#### net/smtp

If an implementation of the
Auth.Start
method returns an empty toServer value,
the package no longer sends
trailing whitespace in the SMTP AUTH command,
which some servers rejected.

#### net/url

The new functions
PathEscape
and
PathUnescape
are similar to the query escaping and unescaping functions but
for path elements.

The new methods
URL.Hostname
and
URL.Port
return the hostname and port fields of a URL,
correctly handling the case where the port may not be present.

The existing method
URL.ResolveReference
now properly handles paths with escaped bytes without losing
the escaping.

The URL type now implements
encoding.BinaryMarshaler and
encoding.BinaryUnmarshaler,
making it possible to process URLs in gob data.

Following RFC 3986,
Parse
now rejects URLs like this_that:other/thing instead of
interpreting them as relative paths (this_that is not a valid scheme).
To force interpretation as a relative path,
such URLs should be prefixed with “./”.
The URL.String method now inserts this prefix as needed.

#### os

The new function
Executable returns
the path name of the running executable.

An attempt to call a method on
an os.File that has
already been closed will now return the new error
value os.ErrClosed.
Previously it returned a system-specific error such
as syscall.EBADF.

On Unix systems, os.Rename
will now return an error when used to rename a directory to an
existing empty directory.
Previously it would fail when renaming to a non-empty directory
but succeed when renaming to an empty directory.
This makes the behavior on Unix correspond to that of other systems.

On Windows, long absolute paths are now transparently converted to
extended-length paths (paths that start with “\\?\”).
This permits the package to work with files whose path names are
longer than 260 characters.

On Windows, os.IsExist
will now return true for the system
error ERROR_DIR_NOT_EMPTY.
This roughly corresponds to the existing handling of the Unix
error ENOTEMPTY.

On Plan 9, files that are not served by #M will now
have ModeDevice set in
the value returned
by FileInfo.Mode.

#### path/filepath

A number of bugs and corner cases on Windows were fixed:
Abs now calls Clean as documented,
Glob now matches
“\\?\c:\*”,
EvalSymlinks now
correctly handles “C:.”, and
Clean now properly
handles a leading “..” in the path.

#### reflect

The new function
Swapper was
added to support sort.Slice.

#### strconv

The Unquote
function now strips carriage returns (\r) in
backquoted raw strings, following the
Go language semantics.

#### syscall

The Getpagesize
now returns the system’s size, rather than a constant value.
Previously it always returned 4KB.

The signature
of Utimes has
changed on Solaris to match all the other Unix systems'
signature. Portable code should continue to use
os.Chtimes instead.

The X__cmsg_data field has been removed from
Cmsghdr.

#### text/template

Template.Execute
can now take a
reflect.Value as its data
argument, and
FuncMap
functions can also accept and return reflect.Value.

#### time

The new function
Until complements
the analogous Since function.

ParseDuration
now accepts long fractional parts.

Parse
now rejects dates before the start of a month, such as June 0;
it already rejected dates beyond the end of the month, such as
June 31 and July 32.

The tzdata database has been updated to version
2016j for systems that don’t already have a local time zone
database.

#### testing

The new method
T.Name
(and B.Name) returns the name of the current
test or benchmark.

The new function
CoverMode
reports the test coverage mode.

Tests and benchmarks are now marked as failed if the race
detector is enabled and a data race occurs during execution.
Previously, individual test cases would appear to pass,
and only the overall execution of the test binary would fail.

The signature of the
MainStart
function has changed, as allowed by the documentation. It is an
internal detail and not part of the Go 1 compatibility promise.
If you’re not calling MainStart directly but see
errors, that likely means you set the
normally-empty GOROOT environment variable and it
doesn’t match the version of your go command’s binary.

#### unicode

SimpleFold
now returns its argument unchanged if the provided input was an invalid rune.
Previously, the implementation failed with an index bounds check panic.
