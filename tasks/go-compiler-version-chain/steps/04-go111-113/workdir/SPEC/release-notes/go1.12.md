# Go 1.12 Release Notes

## Introduction to Go 1.12

The latest Go release, version 1.12, arrives six months after Go 1.11.
Most of its changes are in the implementation of the toolchain, runtime, and libraries.
As always, the release maintains the Go 1 promise of compatibility.
We expect almost all Go programs to continue to compile and run as before.

## Changes to the language

There are no changes to the language specification.

## Ports

The race detector is now supported on linux/arm64.

Go 1.12 is the last release that is supported on FreeBSD 10.x, which has
already reached end-of-life. Go 1.13 will require FreeBSD 11.2+ or FreeBSD
12.0+.
FreeBSD 12.0+ requires a kernel with the COMPAT_FREEBSD11 option set (this is the default).

cgo is now supported on linux/ppc64.

hurd is now a recognized value for GOOS, reserved
for the GNU/Hurd system for use with gccgo.

### Windows

Go’s new windows/arm port supports running Go on Windows 10
IoT Core on 32-bit ARM chips such as the Raspberry Pi 3.

### AIX

Go now supports AIX 7.2 and later on POWER8 architectures (aix/ppc64). External linking, cgo, pprof and the race detector aren’t yet supported.

### Darwin

Go 1.12 is the last release that will run on macOS 10.10 Yosemite.
Go 1.13 will require macOS 10.11 El Capitan or later.

libSystem is now used when making syscalls on Darwin,
ensuring forward-compatibility with future versions of macOS and iOS.
The switch to libSystem triggered additional App Store
checks for private API usage. Since it is considered private,
syscall.Getdirentries now always fails with
ENOSYS on iOS.
Additionally, syscall.Setrlimit
reports invalid argument in places where it historically
succeeded. These consequences are not specific to Go and users should expect
behavioral parity with libSystem’s implementation going forward.

## Tools

### go tool vet no longer supported

The go vet command has been rewritten to serve as the
base for a range of different source code analysis tools. See
the golang.org/x/tools/go/analysis
package for details. A side-effect is that go tool vet
is no longer supported. External tools that use go tool vet must be changed to use go vet. Using go vet instead of go tool vet should work with all supported versions of Go.

As part of this change, the experimental -shadow option
is no longer available with go vet. Checking for
variable shadowing may now be done using

```
go get -u golang.org/x/tools/go/analysis/passes/shadow/cmd/shadow
go vet -vettool=$(which shadow)

### Tour

The Go tour is no longer included in the main binary distribution. To
run the tour locally, instead of running go tool tour,
manually install it:

```
go get -u golang.org/x/tour
tour

### Build cache requirement

The build cache is now
required as a step toward eliminating
$GOPATH/pkg. Setting the environment variable
GOCACHE=off will cause go commands that write to the
cache to fail.

### Binary-only packages

Go 1.12 is the last release that will support binary-only packages.

### Cgo

Go 1.12 will translate the C type EGLDisplay to the Go type uintptr.
This change is similar to how Go 1.10 and newer treats Darwin’s CoreFoundation
and Java’s JNI types. See the
cgo documentation
for more information.

Mangled C names are no longer accepted in packages that use Cgo. Use the Cgo
names instead. For example, use the documented cgo name C.char
rather than the mangled name _Ctype_char that cgo generates.

### Modules

When GO111MODULE is set to on, the go
command now supports module-aware operations outside of a module directory,
provided that those operations do not need to resolve import paths relative to
the current directory or explicitly edit the go.mod file.
Commands such as go get,
go list, and
go mod download behave as if in a
module with initially-empty requirements.
In this mode, go env GOMOD reports
the system’s null device (/dev/null or NUL).

go commands that download and extract modules are now safe to
invoke concurrently.
The module cache (GOPATH/pkg/mod) must reside in a filesystem that
supports file locking.

The go directive in a go.mod file now indicates the
version of the language used by the files within that module.
It will be set to the current release
(go 1.12) if no existing version is
present.
If the go directive for a module specifies a
version newer than the toolchain in use, the go command
will attempt to build the packages regardless, and will note the mismatch only if
that build fails.

This changed use of the go directive means that if you
use Go 1.12 to build a module, thus recording go 1.12
in the go.mod file, you will get an error when
attempting to build the same module with Go 1.11 through Go 1.11.3.
Go 1.11.4 or later will work fine, as will releases older than Go 1.11.
If you must use Go 1.11 through 1.11.3, you can avoid the problem by
setting the language version to 1.11, using the Go 1.12 go tool,
via go mod edit -go=1.11.

When an import cannot be resolved using the active modules,
the go command will now try to use the modules mentioned in the
main module’s replace directives before consulting the module
cache and the usual network sources.
If a matching replacement is found but the replace directive does
not specify a version, the go command uses a pseudo-version
derived from the zero time.Time (such
as v0.0.0-00010101000000-000000000000).

### Compiler toolchain

The compiler’s live variable analysis has improved. This may mean that
finalizers will be executed sooner in this release than in previous
releases. If that is a problem, consider the appropriate addition of a
runtime.KeepAlive call.

More functions are now eligible for inlining by default, including
functions that do nothing but call another function.
This extra inlining makes it additionally important to use
runtime.CallersFrames
instead of iterating over the result of
runtime.Callers directly.

```
// Old code which no longer works correctly (it will miss inlined call frames).
var pcs [10]uintptr
n := runtime.Callers(1, pcs[:])
for _, pc := range pcs[:n] {
    f := runtime.FuncForPC(pc)
    if f != nil {
        fmt.Println(f.Name())
    }
}

```
// New code which will work correctly.
var pcs [10]uintptr
n := runtime.Callers(1, pcs[:])
frames := runtime.CallersFrames(pcs[:n])
for {
    frame, more := frames.Next()
    fmt.Println(frame.Function)
    if !more {
        break
    }
}

Wrappers generated by the compiler to implement method expressions
are no longer reported
by runtime.CallersFrames
and runtime.Stack. They
are also not printed in panic stack traces.
This change aligns the gc toolchain to match
the gccgo toolchain, which already elided such wrappers
from stack traces.
Clients of these APIs might need to adjust for the missing
frames. For code that must interoperate between 1.11 and 1.12
releases, you can replace the method expression x.M
with the function literal func (...) { x.M(...) } .

The compiler now accepts a -lang flag to set the Go language
version to use. For example, -lang=go1.8 causes the compiler to
emit an error if the program uses type aliases, which were added in Go 1.9.
Language changes made before Go 1.12 are not consistently enforced.

The compiler toolchain now uses different conventions to call Go
functions and assembly functions. This should be invisible to users,
except for calls that simultaneously cross between Go and
assembly and cross a package boundary. If linking results
in an error like “relocation target not defined for ABIInternal (but
is defined for ABI0)”, please refer to the
compatibility section
of the ABI design document.

There have been many improvements to the DWARF debug information
produced by the compiler, including improvements to argument
printing and variable location information.

Go programs now also maintain stack frame pointers on linux/arm64
for the benefit of profiling tools like perf. The frame pointer
maintenance has a small run-time overhead that varies but averages around 3%.
To build a toolchain that does not use frame pointers, set
GOEXPERIMENT=noframepointer when running make.bash.

The obsolete “safe” compiler mode (enabled by the -u gcflag) has been removed.

### godoc and go doc

In Go 1.12, godoc no longer has a command-line interface and
is only a web server. Users should use go doc
for command-line help output instead. Go 1.12 is the last release that will
include the godoc webserver; in Go 1.13 it will be available
via go get.

go doc now supports the -all flag,
which will cause it to print all exported APIs and their documentation,
as the godoc command line used to do.

go doc also now includes the -src flag,
which will show the target’s source code.

### Trace

The trace tool now supports plotting mutator utilization curves,
including cross-references to the execution trace. These are useful
for analyzing the impact of the garbage collector on application
latency and throughput.

### Assembler

On arm64, the platform register was renamed from
R18 to R18_PLATFORM to prevent accidental
use, as the OS could choose to reserve this register.

## Runtime

Go 1.12 significantly improves the performance of sweeping when a
large fraction of the heap remains live. This reduces allocation
latency immediately following a garbage collection.

The Go runtime now releases memory back to the operating system more
aggressively, particularly in response to large allocations that
can’t reuse existing heap space.

The Go runtime’s timer and deadline code is faster and scales better
with higher numbers of CPUs. In particular, this improves the
performance of manipulating network connection deadlines.

On Linux, the runtime now uses MADV_FREE to release unused
memory. This is more efficient but may result in higher reported
RSS. The kernel will reclaim the unused data when it is needed.
To revert to the Go 1.11 behavior (MADV_DONTNEED), set the
environment variable GODEBUG=madvdontneed=1.

Adding cpu.extension=off to the
GODEBUG environment
variable now disables the use of optional CPU instruction
set extensions in the standard library and runtime. This is not
yet supported on Windows.

Go 1.12 improves the accuracy of memory profiles by fixing
overcounting of large heap allocations.

Tracebacks, runtime.Caller,
and runtime.Callers no longer include
compiler-generated initialization functions. Doing a traceback
during the initialization of a global variable will now show a
function named PKG.init.ializers.

## Standard library

### TLS 1.3

Go 1.12 adds opt-in support for TLS 1.3 in the crypto/tls package as
specified by RFC 8446. It can
be enabled by adding the value tls13=1 to the GODEBUG
environment variable. It will be enabled by default in Go 1.13.

To negotiate TLS 1.3, make sure you do not set an explicit MaxVersion in
Config and run your program with
the environment variable GODEBUG=tls13=1 set.

All TLS 1.2 features except TLSUnique in
ConnectionState
and renegotiation are available in TLS 1.3 and provide equivalent or
better security and performance. Note that even though TLS 1.3 is backwards
compatible with previous versions, certain legacy systems might not work
correctly when attempting to negotiate it. RSA certificate keys too small
to be secure (including 512-bit keys) will not work with TLS 1.3.

TLS 1.3 cipher suites are not configurable. All supported cipher suites are
safe, and if PreferServerCipherSuites is set in
Config the preference order
is based on the available hardware.

Early data (also called “0-RTT mode”) is not currently supported as a
client or server. Additionally, a Go 1.12 server does not support skipping
unexpected early data if a client sends it. Since TLS 1.3 0-RTT mode
involves clients keeping state regarding which servers support 0-RTT,
a Go 1.12 server cannot be part of a load-balancing pool where some other
servers do support 0-RTT. If switching a domain from a server that supported
0-RTT to a Go 1.12 server, 0-RTT would have to be disabled for at least the
lifetime of the issued session tickets before the switch to ensure
uninterrupted operation.

In TLS 1.3 the client is the last one to speak in the handshake, so if it causes
an error to occur on the server, it will be returned on the client by the first
Read, not by
Handshake. For
example, that will be the case if the server rejects the client certificate.
Similarly, session tickets are now post-handshake messages, so are only
received by the client upon its first
Read.

### Minor changes to the library

As always, there are various minor changes and updates to the library,
made with the Go 1 promise of compatibility
in mind.

#### bufio

Reader’s UnreadRune and
UnreadByte methods will now return an error
if they are called after Peek.

#### bytes

The new function ReplaceAll returns a copy of
a byte slice with all non-overlapping instances of a value replaced by another.

A pointer to a zero-value Reader is now
functionally equivalent to NewReader(nil).
Prior to Go 1.12, the former could not be used as a substitute for the latter in all cases.

#### crypto/rand

A warning will now be printed to standard error the first time
Reader.Read is blocked for more than 60 seconds waiting
to read entropy from the kernel.

On FreeBSD, Reader now uses the getrandom
system call if available, /dev/urandom otherwise.

#### crypto/rc4

This release removes the assembly implementations, leaving only
the pure Go version. The Go compiler generates code that is
either slightly better or slightly worse, depending on the exact
CPU. RC4 is insecure and should only be used for compatibility
with legacy systems.

#### crypto/tls

If a client sends an initial message that does not look like TLS, the server
will no longer reply with an alert, and it will expose the underlying
net.Conn in the new field Conn of
RecordHeaderError.

#### database/sql

A query cursor can now be obtained by passing a
*Rows
value to the Row.Scan method.

#### expvar

The new Delete method allows
for deletion of key/value pairs from a Map.

#### fmt

Maps are now printed in key-sorted order to ease testing. The ordering rules are:

  - When applicable, nil compares low

  - ints, floats, and strings order by <

  - NaN compares less than non-NaN floats

  - bool compares false before true

  - Complex compares real, then imaginary

  - Pointers compare by machine address

  - Channel values compare by machine address

  - Structs compare each field in turn

  - Arrays compare each element in turn

  - Interface values compare first by reflect.Type describing the concrete type
and then by concrete value as described in the previous rules.

When printing maps, non-reflexive key values like NaN were previously
displayed as <nil>. As of this release, the correct values are printed.

#### go/doc

To address some outstanding issues in cmd/doc,
this package has a new Mode bit,
PreserveAST, which controls whether AST data is cleared.

#### go/token

The File type has a new
LineStart field,
which returns the position of the start of a given line. This is especially useful
in programs that occasionally handle non-Go files, such as assembly, but wish to use
the token.Pos mechanism to identify file positions.

#### image

The RegisterFormat function is now safe for concurrent use.

#### image/png

Paletted images with fewer than 16 colors now encode to smaller outputs.

#### io

The new StringWriter interface wraps the
WriteString function.

#### math

The functions
Sin,
Cos,
Tan,
and Sincos now
apply Payne-Hanek range reduction to huge arguments. This
produces more accurate answers, but they will not be bit-for-bit
identical with the results in earlier releases.

#### math/bits

New extended precision operations Add, Sub, Mul, and Div are available in uint, uint32, and uint64 versions.

#### net

The
Dialer.DualStack setting is now ignored and deprecated;
RFC 6555 Fast Fallback (“Happy Eyeballs”) is now enabled by default. To disable, set
Dialer.FallbackDelay to a negative value.

Similarly, TCP keep-alives are now enabled by default if
Dialer.KeepAlive is zero.
To disable, set it to a negative value.

On Linux, the splice system call is now used when copying from a
UnixConn to a
TCPConn.

#### net/http

The HTTP server now rejects misdirected HTTP requests to HTTPS servers with a plaintext “400 Bad Request” response.

The new Client.CloseIdleConnections
method calls the Client’s underlying Transport’s CloseIdleConnections
if it has one.

The Transport no longer rejects HTTP responses which declare
HTTP Trailers but don’t use chunked encoding. Instead, the declared trailers are now just ignored.

The Transport no longer handles MAX_CONCURRENT_STREAMS values
advertised from HTTP/2 servers as strictly as it did during Go 1.10 and Go 1.11. The default behavior is now back
to how it was in Go 1.9: each connection to a server can have up to MAX_CONCURRENT_STREAMS requests
active and then new TCP connections are created as needed. In Go 1.10 and Go 1.11 the http2 package
would block and wait for requests to finish instead of creating new connections.
To get the stricter behavior back, import the
golang.org/x/net/http2 package
directly and set
Transport.StrictMaxConcurrentStreams to
true.

#### net/url

Parse,
ParseRequestURI,
and
URL.Parse
now return an
error for URLs containing ASCII control characters, which includes NULL,
tab, and newlines.

#### net/http/httputil

The ReverseProxy now automatically
proxies WebSocket requests.

#### os

The new ProcessState.ExitCode method
returns the process’s exit code.

ModeCharDevice has been added to the ModeType bitmask, allowing for
ModeDevice | ModeCharDevice to be recovered when masking a
FileMode with ModeType.

The new function UserHomeDir returns the
current user’s home directory.

RemoveAll now supports paths longer than 4096 characters
on most Unix systems.

File.Sync now uses F_FULLFSYNC on macOS
to correctly flush the file contents to permanent storage.
This may cause the method to run more slowly than in previous releases.

File now supports
a SyscallConn
method returning
a syscall.RawConn
interface value. This may be used to invoke system-specific
operations on the underlying file descriptor.

#### path/filepath

The IsAbs function now returns true when passed
a reserved filename on Windows such as NUL.
List of reserved names.

#### reflect

A new MapIter type is
an iterator for ranging over a map. This type is exposed through the
Value type’s new
MapRange method.
This follows the same iteration semantics as a range statement, with Next
to advance the iterator, and Key/Value to access each entry.

#### regexp

Copy is no longer necessary
to avoid lock contention, so it has been given a partial deprecation comment.
Copy
may still be appropriate if the reason for its use is to make two copies with
different Longest settings.

#### runtime/debug

A new BuildInfo type
exposes the build information read from the running binary, available only in
binaries built with module support. This includes the main package path, main
module information, and the module dependencies. This type is given through the
ReadBuildInfo function
on BuildInfo.

#### strings

The new function ReplaceAll returns a copy of
a string with all non-overlapping instances of a value replaced by another.

A pointer to a zero-value Reader is now
functionally equivalent to NewReader(nil).
Prior to Go 1.12, the former could not be used as a substitute for the latter in all cases.

The new Builder.Cap method returns the capacity of the builder’s underlying byte slice.

The character mapping functions Map,
Title,
ToLower,
ToLowerSpecial,
ToTitle,
ToTitleSpecial,
ToUpper, and
ToUpperSpecial
now always guarantee to return valid UTF-8. In earlier releases, if the input was invalid UTF-8 but no character replacements
needed to be applied, these routines incorrectly returned the invalid UTF-8 unmodified.

#### syscall

64-bit inodes are now supported on FreeBSD 12. Some types have been adjusted accordingly.

The Unix socket
(AF_UNIX)
address family is now supported for compatible versions of Windows.

The new function Syscall18
has been introduced for Windows, allowing for calls with up to 18 arguments.

#### syscall/js

The Callback type and NewCallback function have been renamed;
they are now called
Func and
FuncOf, respectively.
This is a breaking change, but WebAssembly support is still experimental
and not yet subject to the
Go 1 compatibility promise. Any code using the
old names will need to be updated.

If a type implements the new
Wrapper
interface,
ValueOf
will use it to return the JavaScript value for that type.

The meaning of the zero
Value
has changed. It now represents the JavaScript undefined value
instead of the number zero.
This is a breaking change, but WebAssembly support is still experimental
and not yet subject to the
Go 1 compatibility promise. Any code relying on
the zero Value
to mean the number zero will need to be updated.

The new
Value.Truthy
method reports the
JavaScript “truthiness”
of a given value.

#### testing

The -benchtime flag now supports setting an explicit iteration count instead of a time when the value ends with an “x”. For example, -benchtime=100x runs the benchmark 100 times.

#### text/template

When executing a template, long context values are no longer truncated in errors.

executing "tmpl" at <.very.deep.context.v...>: map has no entry for key "notpresent"

is now

executing "tmpl" at <.very.deep.context.value.notpresent>: map has no entry for key "notpresent"

If a user-defined function called by a template panics, the
panic is now caught and returned as an error by
the Execute or ExecuteTemplate method.

#### time

The time zone database in $GOROOT/lib/time/zoneinfo.zip
has been updated to version 2018i. Note that this ZIP file is
only used if a time zone database is not provided by the operating
system.

#### unsafe

It is invalid to convert a nil unsafe.Pointer to uintptr and back with arithmetic.
(This was already invalid, but will now cause the compiler to misbehave.)
