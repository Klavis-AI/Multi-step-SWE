# Go 1.7 Release Notes

## Introduction to Go 1.7

The latest Go release, version 1.7, arrives six months after 1.6.
Most of its changes are in the implementation of the toolchain, runtime, and libraries.
There is one minor change to the language specification.
As always, the release maintains the Go 1 promise of compatibility.
We expect almost all Go programs to continue to compile and run as before.

The release adds a port to IBM LinuxOne;
updates the x86-64 compiler back end to generate more efficient code;
includes the context package, promoted from the
x/net subrepository
and now used in the standard library;
and adds support in the testing package for
creating hierarchies of tests and benchmarks.
The release also finalizes the vendoring support
started in Go 1.5, making it a standard feature.

## Changes to the language

There is one tiny language change in this release.
The section on terminating statements
clarifies that to determine whether a statement list ends in a terminating statement,
the “final non-empty statement” is considered the end,
matching the existing behavior of the gc and gccgo compiler toolchains.
In earlier releases the definition referred only to the “final statement,”
leaving the effect of trailing empty statements at the least unclear.
The go/types
package has been updated to match the gc and gccgo compiler toolchains
in this respect.
This change has no effect on the correctness of existing programs.

## Ports

Go 1.7 adds support for macOS 10.12 Sierra.
Binaries built with versions of Go before 1.7 will not work
correctly on Sierra.

Go 1.7 adds an experimental port to Linux on z Systems (linux/s390x)
and the beginning of a port to Plan 9 on ARM (plan9/arm).

The experimental ports to Linux on 64-bit MIPS (linux/mips64 and linux/mips64le)
added in Go 1.6 now have full support for cgo and external linking.

The experimental port to Linux on little-endian 64-bit PowerPC (linux/ppc64le)
now requires the POWER8 architecture or later.
Big-endian 64-bit PowerPC (linux/ppc64) only requires the
POWER5 architecture.

The OpenBSD port now requires OpenBSD 5.6 or later, for access to the getentropy(2) system call.

### Known Issues

There are some instabilities on FreeBSD that are known but not understood.
These can lead to program crashes in rare cases.
See issue 16136,
issue 15658,
and issue 16396.
Any help in solving these FreeBSD-specific issues would be appreciated.

## Tools

### Assembler

For 64-bit ARM systems, the vector register names have been
corrected to V0 through V31;
previous releases incorrectly referred to them as V32 through V63.

For 64-bit x86 systems, the following instructions have been added:
PCMPESTRI,
RORXL,
RORXQ,
VINSERTI128,
VPADDD,
VPADDQ,
VPALIGNR,
VPBLENDD,
VPERM2F128,
VPERM2I128,
VPOR,
VPSHUFB,
VPSHUFD,
VPSLLD,
VPSLLDQ,
VPSLLQ,
VPSRLD,
VPSRLDQ,
and
VPSRLQ.

### Compiler Toolchain

This release includes a new code generation back end for 64-bit x86 systems,
following a proposal from 2015
that has been under development since then.
The new back end, based on
SSA,
generates more compact, more efficient code
and provides a better platform for optimizations
such as bounds check elimination.
The new back end reduces the CPU time required by
our benchmark programs by 5-35%.

For this release, the new back end can be disabled by passing
-ssa=0 to the compiler.
If you find that your program compiles or runs successfully
only with the new back end disabled, please
file a bug report.

The format of exported metadata written by the compiler in package archives has changed:
the old textual format has been replaced by a more compact binary format.
This results in somewhat smaller package archives and fixes a few
long-standing corner case bugs.

For this release, the new export format can be disabled by passing
-newexport=0 to the compiler.
If you find that your program compiles or runs successfully
only with the new export format disabled, please
file a bug report.

The linker’s -X option no longer supports the unusual two-argument form
-X name value,
as announced in the Go 1.6 release
and in warnings printed by the linker.
Use -X name=value instead.

The compiler and linker have been optimized and run significantly faster in this release than in Go 1.6,
although they are still slower than we would like and will continue to be optimized in future releases.

Due to changes across the compiler toolchain and standard library,
binaries built with this release should typically be smaller than binaries
built with Go 1.6,
sometimes by as much as 20-30%.

On x86-64 systems, Go programs now maintain stack frame pointers
as expected by profiling tools like Linux’s perf and Intel’s VTune,
making it easier to analyze and optimize Go programs using these tools.
The frame pointer maintenance has a small run-time overhead that varies
but averages around 2%. We hope to reduce this cost in future releases.
To build a toolchain that does not use frame pointers, set
GOEXPERIMENT=noframepointer when running
make.bash, make.bat, or make.rc.

### Cgo

Packages using cgo may now include
Fortran source files (in addition to C, C++, Objective C, and SWIG),
although the Go bindings must still use C language APIs.

Go bindings may now use a new helper function C.CBytes.
In contrast to C.CString, which takes a Go string
and returns a *C.byte (a C char*),
C.CBytes takes a Go []byte
and returns an unsafe.Pointer (a C void*).

Packages and binaries built using cgo have in past releases
produced different output on each build,
due to the embedding of temporary directory names.
When using this release with
new enough versions of GCC or Clang
(those that support the -fdebug-prefix-map option),
those builds should finally be deterministic.

### Gccgo

Due to the alignment of Go’s semiannual release schedule with GCC’s annual release schedule,
GCC release 6 contains the Go 1.6.1 version of gccgo.
The next release, GCC 7, will likely have the Go 1.8 version of gccgo.

### Go command

The go command’s basic operation
is unchanged, but there are a number of changes worth noting.

This release removes support for the GO15VENDOREXPERIMENT environment variable,
as announced in the Go 1.6 release.
Vendoring support
is now a standard feature of the go command and toolchain.

The Package data structure made available to
“go list” now includes a
StaleReason field explaining why a particular package
is or is not considered stale (in need of rebuilding).
This field is available to the -f or -json
options and is useful for understanding why a target is being rebuilt.

The “go get” command now supports
import paths referring to git.openstack.org.

This release adds experimental, minimal support for building programs using
binary-only packages,
packages distributed in binary form
without the corresponding source code.
This feature is needed in some commercial settings
but is not intended to be fully integrated into the rest of the toolchain.
For example, tools that assume access to complete source code
will not work with such packages, and there are no plans to support
such packages in the “go get” command.

### Go doc

The “go doc” command
now groups constructors with the type they construct,
following godoc.

### Go vet

The “go vet” command
has more accurate analysis in its -copylock and -printf checks,
and a new -tests check that checks the name and signature of likely test functions.
To avoid confusion with the new -tests check, the old, unadvertised
-test option has been removed; it was equivalent to -all -shadow.

The vet command also has a new check,
-lostcancel, which detects failure to call the
cancellation function returned by the WithCancel,
WithTimeout, and WithDeadline functions in
Go 1.7’s new context package (see below).
Failure to call the function prevents the new Context
from being reclaimed until its parent is canceled.
(The background context is never canceled.)

### Go tool dist

The new subcommand “go tool dist list”
prints all supported operating system/architecture pairs.

### Go tool trace

The “go tool trace” command,
introduced in Go 1.5,
has been refined in various ways.

First, collecting traces is significantly more efficient than in past releases.
In this release, the typical execution-time overhead of collecting a trace is about 25%;
in past releases it was at least 400%.
Second, trace files now include file and line number information,
making them more self-contained and making the
original executable optional when running the trace tool.
Third, the trace tool now breaks up large traces to avoid limits
in the browser-based viewer.

Although the trace file format has changed in this release,
the Go 1.7 tools can still read traces from earlier releases.

## Performance

As always, the changes are so general and varied that precise statements
about performance are difficult to make.
Most programs should run a bit faster,
due to speedups in the garbage collector and
optimizations in the core library.
On x86-64 systems, many programs will run significantly faster,
due to improvements in generated code brought by the
new compiler back end.
As noted above, in our own benchmarks,
the code generation changes alone typically reduce program CPU time by 5-35%.

There have been significant optimizations bringing more than 10% improvements
to implementations in the
crypto/sha1,
crypto/sha256,
encoding/binary,
fmt,
hash/adler32,
hash/crc32,
hash/crc64,
image/color,
math/big,
strconv,
strings,
unicode,
and
unicode/utf16
packages.

Garbage collection pauses should be significantly shorter than they
were in Go 1.6 for programs with large numbers of idle goroutines,
substantial stack size fluctuation, or large package-level variables.

## Standard library

### Context

Go 1.7 moves the golang.org/x/net/context package
into the standard library as context.
This allows the use of contexts for cancellation, timeouts, and passing
request-scoped data in other standard library packages,
including
net,
net/http,
and
os/exec,
as noted below.

For more information about contexts, see the
package documentation
and the Go blog post
“Go Concurrent Patterns: Context.”

### HTTP Tracing

Go 1.7 introduces net/http/httptrace,
a package that provides mechanisms for tracing events within HTTP requests.

### Testing

The testing package now supports the definition
of tests with subtests and benchmarks with sub-benchmarks.
This support makes it easy to write table-driven benchmarks
and to create hierarchical tests.
It also provides a way to share common setup and tear-down code.
See the package documentation for details.

### Runtime

All panics started by the runtime now use panic values
that implement both the
builtin error,
and
runtime.Error,
as
required by the language specification.

During panics, if a signal’s name is known, it will be printed in the stack trace.
Otherwise, the signal’s number will be used, as it was before Go1.7.

The new function
KeepAlive
provides an explicit mechanism for declaring
that an allocated object must be considered reachable
at a particular point in a program,
typically to delay the execution of an associated finalizer.

The new function
CallersFrames
translates a PC slice obtained from
Callers
into a sequence of frames corresponding to the call stack.
This new API should be preferred instead of direct use of
FuncForPC,
because the frame sequence can more accurately describe
call stacks with inlined function calls.

The new function
SetCgoTraceback
facilitates tighter integration between Go and C code executing
in the same process called using cgo.

On 32-bit systems, the runtime can now use memory allocated
by the operating system anywhere in the address space,
eliminating the
“memory allocated by OS not in usable range” failure
common in some environments.

The runtime can now return unused memory to the operating system on
all architectures.
In Go 1.6 and earlier, the runtime could not
release memory on ARM64, 64-bit PowerPC, or MIPS.

On Windows, Go programs in Go 1.5 and earlier forced
the global Windows timer resolution to 1ms at startup
by calling timeBeginPeriod(1).
Changing the global timer resolution caused problems on some systems,
and testing suggested that the call was not needed for good scheduler performance,
so Go 1.6 removed the call.
Go 1.7 brings the call back: under some workloads the call
is still needed for good scheduler performance.

### Minor changes to the library

As always, there are various minor changes and updates to the library,
made with the Go 1 promise of compatibility
in mind.

#### bufio

In previous releases of Go, if
Reader’s
Peek method
were asked for more bytes than fit in the underlying buffer,
it would return an empty slice and the error ErrBufferFull.
Now it returns the entire underlying buffer, still accompanied by the error ErrBufferFull.

#### bytes

The new functions
ContainsAny and
ContainsRune
have been added for symmetry with
the strings package.

In previous releases of Go, if
Reader’s
Read method
were asked for zero bytes with no data remaining, it would
return a count of 0 and no error.
Now it returns a count of 0 and the error
io.EOF.

The
Reader type has a new method
Reset to allow reuse of a Reader.

#### compress/flate

There are many performance optimizations throughout the package.
Decompression speed is improved by about 10%,
while compression for DefaultCompression is twice as fast.

In addition to those general improvements,
the
BestSpeed
compressor has been replaced entirely and uses an
algorithm similar to Snappy,
resulting in about a 2.5X speed increase,
although the output can be 5-10% larger than with the previous algorithm.

There is also a new compression level
HuffmanOnly
that applies Huffman but not Lempel-Ziv encoding.
Forgoing Lempel-Ziv encoding means that
HuffmanOnly runs about 3X faster than the new BestSpeed
but at the cost of producing compressed outputs that are 20-40% larger than those
generated by the new BestSpeed.

It is important to note that both
BestSpeed and HuffmanOnly produce a compressed output that is
RFC 1951 compliant.
In other words, any valid DEFLATE decompressor will continue to be able to decompress these outputs.

Lastly, there is a minor change to the decompressor’s implementation of
io.Reader. In previous versions,
the decompressor deferred reporting
io.EOF until exactly no more bytes could be read.
Now, it reports
io.EOF more eagerly when reading the last set of bytes.

#### crypto/tls

The TLS implementation sends the first few data packets on each connection
using small record sizes, gradually increasing to the TLS maximum record size.
This heuristic reduces the amount of data that must be received before
the first packet can be decrypted, improving communication latency over
low-bandwidth networks.
Setting
Config’s
DynamicRecordSizingDisabled field to true
forces the behavior of Go 1.6 and earlier, where packets are
as large as possible from the start of the connection.

The TLS client now has optional, limited support for server-initiated renegotiation,
enabled by setting the
Config’s
Renegotiation field.
This is needed for connecting to many Microsoft Azure servers.

The errors returned by the package now consistently begin with a
tls: prefix.
In past releases, some errors used a crypto/tls: prefix,
some used a tls: prefix, and some had no prefix at all.

When generating self-signed certificates, the package no longer sets the
“Authority Key Identifier” field by default.

#### crypto/x509

The new function
SystemCertPool
provides access to the entire system certificate pool if available.
There is also a new associated error type
SystemRootsError.

#### debug/dwarf

The
Reader type’s new
SeekPC method and the
Data type’s new
Ranges method
help to find the compilation unit to pass to a
LineReader
and to identify the specific function for a given program counter.

#### debug/elf

The new
R_390 relocation type
and its many predefined constants
support the S390 port.

#### encoding/asn1

The ASN.1 decoder now rejects non-minimal integer encodings.
This may cause the package to reject some invalid but formerly accepted ASN.1 data.

#### encoding/json

The
Encoder’s new
SetIndent method
sets the indentation parameters for JSON encoding,
like in the top-level
Indent function.

The
Encoder’s new
SetEscapeHTML method
controls whether the
&, <, and >
characters in quoted strings should be escaped as
\u0026, \u003c, and \u003e,
respectively.
As in previous releases, the encoder defaults to applying this escaping,
to avoid certain problems that can arise when embedding JSON in HTML.

In earlier versions of Go, this package only supported encoding and decoding
maps using keys with string types.
Go 1.7 adds support for maps using keys with integer types:
the encoding uses a quoted decimal representation as the JSON key.
Go 1.7 also adds support for encoding maps using non-string keys that implement
the MarshalText
(see
encoding.TextMarshaler)
method,
as well as support for decoding maps using non-string keys that implement
the UnmarshalText
(see
encoding.TextUnmarshaler)
method.
These methods are ignored for keys with string types in order to preserve
the encoding and decoding used in earlier versions of Go.

When encoding a slice of typed bytes,
Marshal
now generates an array of elements encoded using
that byte type’s
MarshalJSON
or
MarshalText
method if present,
only falling back to the default base64-encoded string data if neither method is available.
Earlier versions of Go accept both the original base64-encoded string encoding
and the array encoding (assuming the byte type also implements
UnmarshalJSON
or
UnmarshalText
as appropriate),
so this change should be semantically backwards compatible with earlier versions of Go,
even though it does change the chosen encoding.

#### go/build

To implement the go command’s new support for binary-only packages
and for Fortran code in cgo-based packages,
the
Package type
adds new fields BinaryOnly, CgoFFLAGS, and FFiles.

#### go/doc

To support the corresponding change in go test described above,
Example struct adds an Unordered field
indicating whether the example may generate its output lines in any order.

#### io

The package adds new constants
SeekStart, SeekCurrent, and SeekEnd,
for use with
Seeker
implementations.
These constants are preferred over os.SEEK_SET, os.SEEK_CUR, and os.SEEK_END,
but the latter will be preserved for compatibility.

#### math/big

The
Float type adds
GobEncode and
GobDecode methods,
so that values of type Float can now be encoded and decoded using the
encoding/gob
package.

#### math/rand

The
Read function and
Rand’s
Read method
now produce a pseudo-random stream of bytes that is consistent and not
dependent on the size of the input buffer.

The documentation clarifies that
Rand’s Seed
and Read methods
are not safe to call concurrently, though the global
functions Seed
and Read are (and have
always been) safe.

#### mime/multipart

The
Writer
implementation now emits each multipart section’s header sorted by key.
Previously, iteration over a map caused the section header to use a
non-deterministic order.

#### net

As part of the introduction of context, the
Dialer type has a new method
DialContext, like
Dial but adding the
context.Context
for the dial operation.
The context is intended to obsolete the Dialer’s
Cancel and Deadline fields,
but the implementation continues to respect them,
for backwards compatibility.

The
IP type’s
String method has changed its result for invalid IP addresses.
In past releases, if an IP byte slice had length other than 0, 4, or 16, String
returned "?".
Go 1.7 adds the hexadecimal encoding of the bytes, as in "?12ab".

The pure Go name resolution
implementation now respects nsswitch.conf’s
stated preference for the priority of DNS lookups compared to
local file (that is, /etc/hosts) lookups.

#### net/http

ResponseWriter’s
documentation now makes clear that beginning to write the response
may prevent future reads on the request body.
For maximal compatibility, implementations are encouraged to
read the request body completely before writing any part of the response.

As part of the introduction of context, the
Request has a new methods
Context, to retrieve the associated context, and
WithContext, to construct a copy of Request
with a modified context.

In the
Server implementation,
Serve records in the request context
both the underlying *Server using the key ServerContextKey
and the local address on which the request was received (a
Addr) using the key LocalAddrContextKey.
For example, the address on which a request received is
req.Context().Value(http.LocalAddrContextKey).(net.Addr).

The server’s Serve method
now only enables HTTP/2 support if the Server.TLSConfig field is nil
or includes "h2" in its TLSConfig.NextProtos.

The server implementation now
pads response codes less than 100 to three digits
as required by the protocol,
so that w.WriteHeader(5) uses the HTTP response
status 005, not just 5.

The server implementation now correctly sends only one “Transfer-Encoding” header when “chunked”
is set explicitly, following RFC 7230.

The server implementation is now stricter about rejecting requests with invalid HTTP versions.
Invalid requests claiming to be HTTP/0.x are now rejected (HTTP/0.9 was never fully supported),
and plaintext HTTP/2 requests other than the “PRI * HTTP/2.0” upgrade request are now rejected as well.
The server continues to handle encrypted HTTP/2 requests.

In the server, a 200 status code is sent back by the timeout handler on an empty
response body, instead of sending back 0 as the status code.

In the client, the
Transport implementation passes the request context
to any dial operation connecting to the remote server.
If a custom dialer is needed, the new Transport field
DialContext is preferred over the existing Dial field,
to allow the transport to supply a context.

The
Transport also adds fields
IdleConnTimeout,
MaxIdleConns,
and
MaxResponseHeaderBytes
to help control client resources consumed
by idle or chatty servers.

A
Client’s configured CheckRedirect function can now
return ErrUseLastResponse to indicate that the
most recent redirect response should be returned as the
result of the HTTP request.
That response is now available to the CheckRedirect function
as req.Response.

Since Go 1, the default behavior of the HTTP client is
to request server-side compression
using the Accept-Encoding request header
and then to decompress the response body transparently,
and this behavior is adjustable using the
Transport’s DisableCompression field.
In Go 1.7, to aid the implementation of HTTP proxies, the
Response’s new
Uncompressed field reports whether
this transparent decompression took place.

DetectContentType
adds support for a few new audio and video content types.

#### net/http/cgi

The
Handler
adds a new field
Stderr
that allows redirection of the child process’s
standard error away from the host process’s
standard error.

#### net/http/httptest

The new function
NewRequest
prepares a new
http.Request
suitable for passing to an
http.Handler during a test.

The
ResponseRecorder’s new
Result method
returns the recorded
http.Response.
Tests that need to check the response’s headers or trailers
should call Result and inspect the response fields
instead of accessing
ResponseRecorder’s HeaderMap directly.

#### net/http/httputil

The
ReverseProxy implementation now responds with “502 Bad Gateway”
when it cannot reach a back end; in earlier releases it responded with “500 Internal Server Error.”

Both
ClientConn and
ServerConn have been documented as deprecated.
They are low-level, old, and unused by Go’s current HTTP stack
and will no longer be updated.
Programs should use
http.Client,
http.Transport,
and
http.Server
instead.

#### net/http/pprof

The runtime trace HTTP handler, installed to handle the path /debug/pprof/trace,
now accepts a fractional number in its seconds query parameter,
allowing collection of traces for intervals smaller than one second.
This is especially useful on busy servers.

#### net/mail

The address parser now allows unescaped UTF-8 text in addresses
following RFC 6532,
but it does not apply any normalization to the result.
For compatibility with older mail parsers,
the address encoder, namely
Address’s
String method,
continues to escape all UTF-8 text following RFC 5322.

The ParseAddress
function and
the AddressParser.Parse
method are stricter.
They used to ignore any characters following an e-mail address, but
will now return an error for anything other than whitespace.

#### net/url

The
URL’s
new ForceQuery field
records whether the URL must have a query string,
in order to distinguish URLs without query strings (like /search)
from URLs with empty query strings (like /search?).

#### os

IsExist now returns true for syscall.ENOTEMPTY,
on systems where that error exists.

On Windows,
Remove now removes read-only files when possible,
making the implementation behave as on
non-Windows systems.

#### os/exec

As part of the introduction of context,
the new constructor
CommandContext
is like
Command but includes a context that can be used to cancel the command execution.

#### os/user

The
Current
function is now implemented even when cgo is not available.

The new
Group type,
along with the lookup functions
LookupGroup and
LookupGroupId
and the new field GroupIds in the User struct,
provides access to system-specific user group information.

#### reflect

Although
Value’s
Field method has always been documented to panic
if the given field number i is out of range, it has instead
silently returned a zero
Value.
Go 1.7 changes the method to behave as documented.

The new
StructOf
function constructs a struct type at run time.
It completes the set of type constructors, joining
ArrayOf,
ChanOf,
FuncOf,
MapOf,
PtrTo,
and
SliceOf.

StructTag’s
new method
Lookup
is like
Get
but distinguishes the tag not containing the given key
from the tag associating an empty string with the given key.

The
Method and
NumMethod
methods of
Type and
Value
no longer return or count unexported methods.

#### strings

In previous releases of Go, if
Reader’s
Read method
were asked for zero bytes with no data remaining, it would
return a count of 0 and no error.
Now it returns a count of 0 and the error
io.EOF.

The
Reader type has a new method
Reset to allow reuse of a Reader.

#### time

Duration’s
time.Duration.String method now reports the zero duration as "0s", not "0".
ParseDuration continues to accept both forms.

The method call time.Local.String() now returns "Local" on all systems;
in earlier releases, it returned an empty string on Windows.

The time zone database in
$GOROOT/lib/time has been updated
to IANA release 2016d.
This fallback database is only used when the system time zone database
cannot be found, for example on Windows.
The Windows time zone abbreviation list has also been updated.

#### syscall

On Linux, the
SysProcAttr struct
(as used in
os/exec.Cmd’s SysProcAttr field)
has a new Unshareflags field.
If the field is nonzero, the child process created by
ForkExec
(as used in exec.Cmd’s Run method)
will call the
unshare(2)
system call before executing the new program.

#### unicode

The unicode package and associated
support throughout the system has been upgraded from version 8.0 to
Unicode 9.0.
