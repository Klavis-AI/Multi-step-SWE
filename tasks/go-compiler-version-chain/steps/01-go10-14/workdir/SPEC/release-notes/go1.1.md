# Go 1.1 Release Notes

## Introduction to Go 1.1

THE RELEASE of Go version 1 (Go 1 or Go 1.0 for short)
in March of 2012 introduced a new period
of stability in the Go language and libraries.
That stability has helped nourish a growing community of Go users
and systems around the world.
Several “point” releases since
then—1.0.1, 1.0.2, and 1.0.3—have been issued.
These point releases fixed known bugs but made
no non-critical changes to the implementation.

This new release, Go 1.1, keeps the promise
of compatibility but adds a couple of significant
(backwards-compatible, of course) language changes, has a long list
of (again, compatible) library changes, and
includes major work on the implementation of the compilers,
libraries, and run-time.
The focus is on performance.
Benchmarking is an inexact science at best, but we see significant,
sometimes dramatic speedups for many of our test programs.
We trust that many of our users’ programs will also see improvements
just by updating their Go installation and recompiling.

This document summarizes the changes between Go 1 and Go 1.1.
Very little if any code will need modification to run with Go 1.1,
although a couple of rare error cases surface with this release
and need to be addressed if they arise.
Details appear below; see the discussion of
64-bit ints and Unicode literals
in particular.

## Changes to the language

The Go compatibility document promises
that programs written to the Go 1 language specification will continue to operate,
and those promises are maintained.
In the interest of firming up the specification, though, there are
details about some error cases that have been clarified.
There are also some new language features.

### Integer division by zero

In Go 1, integer division by a constant zero produced a run-time panic:

```
func f(x int) int {
    return x/0
}

In Go 1.1, an integer division by constant zero is not a legal program, so it is a compile-time error.

### Surrogates in Unicode literals

The definition of string and rune literals has been refined to exclude surrogate halves from the
set of valid Unicode code points.
See the Unicode section for more information.

### Method values

Go 1.1 now implements
method values,
which are functions that have been bound to a specific receiver value.
For instance, given a
Writer
value w,
the expression
w.Write,
a method value, is a function that will always write to w; it is equivalent to
a function literal closing over w:

```
func (p []byte) (n int, err error) {
    return w.Write(p)
}

Method values are distinct from method expressions, which generate functions
from methods of a given type; the method expression (*bufio.Writer).Write
is equivalent to a function with an extra first argument, a receiver of type
(*bufio.Writer):

```
func (w *bufio.Writer, p []byte) (n int, err error) {
    return w.Write(p)
}

Updating: No existing code is affected; the change is strictly backward-compatible.

### Return requirements

Before Go 1.1, a function that returned a value needed an explicit “return”
or call to panic at
the end of the function; this was a simple way to make the programmer
be explicit about the meaning of the function. But there are many cases
where a final “return” is clearly unnecessary, such as a function with
only an infinite “for” loop.

In Go 1.1, the rule about final “return” statements is more permissive.
It introduces the concept of a
terminating statement,
a statement that is guaranteed to be the last one a function executes.
Examples include
“for” loops with no condition and “if-else”
statements in which each half ends in a “return”.
If the final statement of a function can be shown syntactically to
be a terminating statement, no final “return” statement is needed.

Note that the rule is purely syntactic: it pays no attention to the values in the
code and therefore requires no complex analysis.

Updating: The change is backward-compatible, but existing code
with superfluous “return” statements and calls to panic may
be simplified manually.
Such code can be identified by go vet.

## Changes to the implementations and tools

### Status of gccgo

The GCC release schedule does not coincide with the Go release schedule, so some skew is inevitable in
gccgo’s releases.
The 4.8.0 version of GCC shipped in March, 2013 and includes a nearly-Go 1.1 version of gccgo.
Its library is a little behind the release, but the biggest difference is that method values are not implemented.
Sometime around July 2013, we expect 4.8.2 of GCC to ship with a gccgo
providing a complete Go 1.1 implementation.

### Command-line flag parsing

In the gc toolchain, the compilers and linkers now use the
same command-line flag parsing rules as the Go flag package, a departure
from the traditional Unix flag parsing. This may affect scripts that invoke
the tool directly.
For example,
go tool 6c -Fw -Dfoo must now be written
go tool 6c -F -w -D foo.

### Size of int on 64-bit platforms

The language allows the implementation to choose whether the int type and
uint types are 32 or 64 bits. Previous Go implementations made int
and uint 32 bits on all systems. Both the gc and gccgo implementations
now make
int and uint 64 bits on 64-bit platforms such as AMD64/x86-64.
Among other things, this enables the allocation of slices with
more than 2 billion elements on 64-bit platforms.

Updating:
Most programs will be unaffected by this change.
Because Go does not allow implicit conversions between distinct
numeric types,
no programs will stop compiling due to this change.
However, programs that contain implicit assumptions
that int is only 32 bits may change behavior.
For example, this code prints a positive number on 64-bit systems and
a negative one on 32-bit systems:

```
x := ^uint32(0) // x is 0xffffffff
i := int(x)     // i is -1 on 32-bit systems, 0xffffffff on 64-bit
fmt.Println(i)

Portable code intending 32-bit sign extension (yielding -1 on all systems)
would instead say:

```
i := int(int32(x))

### Heap size on 64-bit architectures

On 64-bit architectures, the maximum heap size has been enlarged substantially,
from a few gigabytes to several tens of gigabytes.
(The exact details depend on the system and may change.)

On 32-bit architectures, the heap size has not changed.

Updating:
This change should have no effect on existing programs beyond allowing them
to run with larger heaps.

### Unicode

To make it possible to represent code points greater than 65535 in UTF-16,
Unicode defines surrogate halves,
a range of code points to be used only in the assembly of large values, and only in UTF-16.
The code points in that surrogate range are illegal for any other purpose.
In Go 1.1, this constraint is honored by the compiler, libraries, and run-time:
a surrogate half is illegal as a rune value, when encoded as UTF-8, or when
encoded in isolation as UTF-16.
When encountered, for example in converting from a rune to UTF-8, it is
treated as an encoding error and will yield the replacement rune,
utf8.RuneError,
U+FFFD.

This program,

```
import "fmt"

func main() {
    fmt.Printf("%+q\n", string(0xD800))
}

printed "\ud800" in Go 1.0, but prints "\ufffd" in Go 1.1.

Surrogate-half Unicode values are now illegal in rune and string constants, so constants such as
'\ud800' and "\ud800" are now rejected by the compilers.
When written explicitly as UTF-8 encoded bytes,
such strings can still be created, as in "\xed\xa0\x80".
However, when such a string is decoded as a sequence of runes, as in a range loop, it will yield only utf8.RuneError
values.

The Unicode byte order mark U+FEFF, encoded in UTF-8, is now permitted as the first
character of a Go source file.
Even though its appearance in the byte-order-free UTF-8 encoding is clearly unnecessary,
some editors add the mark as a kind of “magic number” identifying a UTF-8 encoded file.

Updating:
Most programs will be unaffected by the surrogate change.
Programs that depend on the old behavior should be modified to avoid the issue.
The byte-order-mark change is strictly backward-compatible.

### Race detector

A major addition to the tools is a race detector, a way to
find bugs in programs caused by concurrent access of the same
variable, where at least one of the accesses is a write.
This new facility is built into the go tool.
For now, it is only available on Linux, Mac OS X, and Windows systems with
64-bit x86 processors.
To enable it, set the -race flag when building or testing your program
(for instance, go test -race).
The race detector is documented in a separate article.

### The gc assemblers

Due to the change of the int to 64 bits and
a new internal representation of functions,
the arrangement of function arguments on the stack has changed in the gc toolchain.
Functions written in assembly will need to be revised at least
to adjust frame pointer offsets.

Updating:
The go vet command now checks that functions implemented in assembly
match the Go function prototypes they implement.

### Changes to the go command

The go command has acquired several
changes intended to improve the experience for new Go users.

First, when compiling, testing, or running Go code, the go command will now give more detailed error messages,
including a list of paths searched, when a package cannot be located.

```
$ go build foo/quxx
can't load package: package foo/quxx: cannot find package "foo/quxx" in any of:
        /home/you/go/src/pkg/foo/quxx (from $GOROOT)
        /home/you/src/foo/quxx (from $GOPATH)

Second, the go get command no longer allows $GOROOT
as the default destination when downloading package source.
To use the go get
command, a valid $GOPATH is now required.

```
$ GOPATH= go get code.google.com/p/foo/quxx
package code.google.com/p/foo/quxx: cannot download, $GOPATH not set. For more details see: go help gopath

Finally, as a result of the previous change, the go get command will also fail
when $GOPATH and $GOROOT are set to the same value.

```
$ GOPATH=$GOROOT go get code.google.com/p/foo/quxx
warning: GOPATH set to GOROOT (/home/you/go) has no effect
package code.google.com/p/foo/quxx: cannot download, $GOPATH must not be set to $GOROOT. For more details see: go help gopath

### Changes to the go test command

The go test
command no longer deletes the binary when run with profiling enabled,
to make it easier to analyze the profile.
The implementation sets the -c flag automatically, so after running,

```
$ go test -cpuprofile cpuprof.out mypackage

the file mypackage.test will be left in the directory where go test was run.

The go test
command can now generate profiling information
that reports where goroutines are blocked, that is,
where they tend to stall waiting for an event such as a channel communication.
The information is presented as a
blocking profile
enabled with the
-blockprofile
option of
go test.
Run go help test for more information.

### Changes to the go fix command

The fix command, usually run as
go fix, no longer applies fixes to update code from
before Go 1 to use Go 1 APIs.
To update pre-Go 1 code to Go 1.1, use a Go 1.0 toolchain
to convert the code to Go 1.0 first.

### Build constraints

The “go1.1” tag has been added to the list of default
build constraints.
This permits packages to take advantage of the new features in Go 1.1 while
remaining compatible with earlier versions of Go.

To build a file only with Go 1.1 and above, add this build constraint:

```
// +build go1.1

To build a file only with Go 1.0.x, use the converse constraint:

```
// +build !go1.1

### Additional platforms

The Go 1.1 toolchain adds experimental support for freebsd/arm,
netbsd/386, netbsd/amd64, netbsd/arm,
openbsd/386 and openbsd/amd64 platforms.

An ARMv6 or later processor is required for freebsd/arm or
netbsd/arm.

Go 1.1 adds experimental support for cgo on linux/arm.

### Cross compilation

When cross-compiling, the go tool will disable cgo
support by default.

To explicitly enable cgo, set CGO_ENABLED=1.

## Performance

The performance of code compiled with the Go 1.1 gc tool suite should be noticeably
better for most Go programs.
Typical improvements relative to Go 1.0 seem to be about 30%-40%, sometimes
much more, but occasionally less or even non-existent.
There are too many small performance-driven tweaks through the tools and libraries
to list them all here, but the following major changes are worth noting:

  - The gc compilers generate better code in many cases, most noticeably for
floating point on the 32-bit Intel architecture.

  - The gc compilers do more in-lining, including for some operations
in the run-time such as append
and interface conversions.

  - There is a new implementation of Go maps with significant reduction in
memory footprint and CPU time.

  - The garbage collector has been made more parallel, which can reduce
latencies for programs running on multiple CPUs.

  - The garbage collector is also more precise, which costs a small amount of
CPU time but can reduce the size of the heap significantly, especially
on 32-bit architectures.

  - Due to tighter coupling of the run-time and network libraries, fewer
context switches are required on network operations.

## Changes to the standard library

### bufio.Scanner

The various routines to scan textual input in the
bufio
package,
ReadBytes,
ReadString
and particularly
ReadLine,
are needlessly complex to use for simple purposes.
In Go 1.1, a new type,
Scanner,
has been added to make it easier to do simple tasks such as
read the input as a sequence of lines or space-delimited words.
It simplifies the problem by terminating the scan on problematic
input such as pathologically long lines, and having a simple
default: line-oriented input, with each line stripped of its terminator.
Here is code to reproduce the input a line at a time:

```
scanner := bufio.NewScanner(os.Stdin)
for scanner.Scan() {
    fmt.Println(scanner.Text()) // Println will add back the final '\n'
}
if err := scanner.Err(); err != nil {
    fmt.Fprintln(os.Stderr, "reading standard input:", err)
}

Scanning behavior can be adjusted through a function to control subdividing the input
(see the documentation for SplitFunc),
but for tough problems or the need to continue past errors, the older interface
may still be required.

### net

The protocol-specific resolvers in the net package were formerly
lax about the network name passed in.
Although the documentation was clear
that the only valid networks for
ResolveTCPAddr
are "tcp",
"tcp4", and "tcp6", the Go 1.0 implementation silently accepted any string.
The Go 1.1 implementation returns an error if the network is not one of those strings.
The same is true of the other protocol-specific resolvers ResolveIPAddr,
ResolveUDPAddr, and
ResolveUnixAddr.

The previous implementation of
ListenUnixgram
returned a
UDPConn as
a representation of the connection endpoint.
The Go 1.1 implementation instead returns a
UnixConn
to allow reading and writing
with its
ReadFrom
and
WriteTo
methods.

The data structures
IPAddr,
TCPAddr, and
UDPAddr
add a new string field called Zone.
Code using untagged composite literals (e.g. net.TCPAddr{ip, port})
instead of tagged literals (net.TCPAddr{IP: ip, Port: port})
will break due to the new field.
The Go 1 compatibility rules allow this change: client code must use tagged literals to avoid such breakages.

Updating:
To correct breakage caused by the new struct field,
go fix will rewrite code to add tags for these types.
More generally, go vet will identify composite literals that
should be revised to use field tags.

### reflect

The reflect package has several significant additions.

It is now possible to run a “select” statement using
the reflect package; see the description of
Select
and
SelectCase
for details.

The new method
Value.Convert
(or
Type.ConvertibleTo)
provides functionality to execute a Go conversion or type assertion operation
on a
Value
(or test for its possibility).

The new function
MakeFunc
creates a wrapper function to make it easier to call a function with existing
Values,
doing the standard Go conversions among the arguments, for instance
to pass an actual int to a formal interface{}.

Finally, the new functions
ChanOf,
MapOf
and
SliceOf
construct new
Types
from existing types, for example to construct the type []T given
only T.

### time

On FreeBSD, Linux, NetBSD, OS X and OpenBSD, previous versions of the
time package
returned times with microsecond precision.
The Go 1.1 implementation on these
systems now returns times with nanosecond precision.
Programs that write to an external format with microsecond precision
and read it back, expecting to recover the original value, will be affected
by the loss of precision.
There are two new methods of Time,
Round
and
Truncate,
that can be used to remove precision from a time before passing it to
external storage.

The new method
YearDay
returns the one-indexed integral day number of the year specified by the time value.

The
Timer
type has a new method
Reset
that modifies the timer to expire after a specified duration.

Finally, the new function
ParseInLocation
is like the existing
Parse
but parses the time in the context of a location (time zone), ignoring
time zone information in the parsed string.
This function addresses a common source of confusion in the time API.

Updating:
Code that needs to read and write times using an external format with
lower precision should be modified to use the new methods.

### Exp and old subtrees moved to go.exp and go.text subrepositories

To make it easier for binary distributions to access them if desired, the exp
and old source subtrees, which are not included in binary distributions,
have been moved to the new go.exp subrepository at
code.google.com/p/go.exp. To access the ssa package,
for example, run

```
$ go get code.google.com/p/go.exp/ssa

and then in Go source,

```
import "code.google.com/p/go.exp/ssa"

The old package exp/norm has also been moved, but to a new repository
go.text, where the Unicode APIs and other text-related packages will
be developed.

### New packages

There are three new packages.

  - The go/format package provides
a convenient way for a program to access the formatting capabilities of the
go fmt command.
It has two functions,
Node to format a Go parser
Node,
and
Source
to reformat arbitrary Go source code into the standard format as provided by the
go fmt command.

  - The net/http/cookiejar package provides the basics for managing HTTP cookies.

  - The runtime/race package provides low-level facilities for data race detection.
It is internal to the race detector and does not otherwise export any user-visible functionality.

### Minor changes to the library

The following list summarizes a number of minor changes to the library, mostly additions.
See the relevant package documentation for more information about each change.

  - The bytes package has two new functions,
TrimPrefix
and
TrimSuffix,
with self-evident properties.
Also, the Buffer type
has a new method
Grow that
provides some control over memory allocation inside the buffer.
Finally, the
Reader type now has a
WriteTo method
so it implements the
io.WriterTo interface.

  - The compress/gzip package has
a new Flush
method for its
Writer
type that flushes its underlying flate.Writer.

  - The crypto/hmac package has a new function,
Equal, to compare two MACs.

  - The crypto/x509 package
now supports PEM blocks (see
DecryptPEMBlock for instance),
and a new function
ParseECPrivateKey to parse elliptic curve private keys.

  - The database/sql package
has a new
Ping
method for its
DB
type that tests the health of the connection.

  - The database/sql/driver package
has a new
Queryer
interface that a
Conn
may implement to improve performance.

  - The encoding/json package’s
Decoder
has a new method
Buffered
to provide access to the remaining data in its buffer,
as well as a new method
UseNumber
to unmarshal a value into the new type
Number,
a string, rather than a float64.

  - The encoding/xml package
has a new function,
EscapeText,
which writes escaped XML output,
and a method on
Encoder,
Indent,
to specify indented output.

  - In the go/ast package, a
new type CommentMap
and associated methods makes it easier to extract and process comments in Go programs.

  - In the go/doc package,
the parser now keeps better track of stylized annotations such as TODO(joe)
throughout the code,
information that the godoc
command can filter or present according to the value of the -notes flag.

  - The undocumented and only partially implemented “noescape” feature of the
html/template
package has been removed; programs that depend on it will break.

  - The image/jpeg package now
reads progressive JPEG files and handles a few more subsampling configurations.

  - The io package now exports the
io.ByteWriter interface to capture the common
functionality of writing a byte at a time.
It also exports a new error, ErrNoProgress,
used to indicate a Read implementation is looping without delivering data.

  - The log/syslog package now provides better support
for OS-specific logging features.

  - The math/big package’s
Int type
now has methods
MarshalJSON
and
UnmarshalJSON
to convert to and from a JSON representation.
Also,
Int
can now convert directly to and from a uint64 using
Uint64
and
SetUint64,
while
Rat
can do the same with float64 using
Float64
and
SetFloat64.

  - The mime/multipart package
has a new method for its
Writer,
SetBoundary,
to define the boundary separator used to package the output.
The Reader also now
transparently decodes any quoted-printable parts and removes
the Content-Transfer-Encoding header when doing so.

  - The
net package’s
ListenUnixgram
function has changed return types: it now returns a
UnixConn
rather than a
UDPConn, which was
clearly a mistake in Go 1.0.
Since this API change fixes a bug, it is permitted by the Go 1 compatibility rules.

  - The net package includes a new type,
Dialer, to supply options to
Dial.

  - The net package adds support for
link-local IPv6 addresses with zone qualifiers, such as fe80::1%lo0.
The address structures IPAddr,
UDPAddr, and
TCPAddr
record the zone in a new field, and functions that expect string forms of these addresses, such as
Dial,
ResolveIPAddr,
ResolveUDPAddr, and
ResolveTCPAddr,
now accept the zone-qualified form.

  - The net package adds
LookupNS to its suite of resolving functions.
LookupNS returns the NS records for a host name.

  - The net package adds protocol-specific
packet reading and writing methods to
IPConn
(ReadMsgIP
and WriteMsgIP) and
UDPConn
(ReadMsgUDP and
WriteMsgUDP).
These are specialized versions of PacketConn’s
ReadFrom and WriteTo methods that provide access to out-of-band data associated
with the packets.

  - The net package adds methods to
UnixConn to allow closing half of the connection
(CloseRead and
CloseWrite),
matching the existing methods of TCPConn.

  - The net/http package includes several new additions.
ParseTime parses a time string, trying
several common HTTP time formats.
The PostFormValue method of
Request is like
FormValue but ignores URL parameters.
The CloseNotifier interface provides a mechanism
for a server handler to discover when a client has disconnected.
The ServeMux type now has a
Handler method to access a path’s
Handler without executing it.
The Transport can now cancel an in-flight request with
CancelRequest.
Finally, the Transport is now more aggressive at closing TCP connections when
a Response.Body is closed before
being fully consumed.

  - The net/mail package has two new functions,
ParseAddress and
ParseAddressList,
to parse RFC 5322-formatted mail addresses into
Address structures.

  - The net/smtp package’s
Client type has a new method,
Hello,
which transmits a HELO or EHLO message to the server.

  - The net/textproto package
has two new functions,
TrimBytes and
TrimString,
which do ASCII-only trimming of leading and trailing spaces.

  - The new method os.FileMode.IsRegular makes it easy to ask if a file is a plain file.

  - The os/signal package has a new function,
Stop, which stops the package delivering
any further signals to the channel.

  - The regexp package
now supports Unix-original leftmost-longest matches through the
Regexp.Longest
method, while
Regexp.Split slices
strings into pieces based on separators defined by the regular expression.

  - The runtime/debug package
has three new functions regarding memory usage.
The FreeOSMemory
function triggers a run of the garbage collector and then attempts to return unused
memory to the operating system;
the ReadGCStats
function retrieves statistics about the collector; and
SetGCPercent
provides a programmatic way to control how often the collector runs,
including disabling it altogether.

  - The sort package has a new function,
Reverse.
Wrapping the argument of a call to
sort.Sort
with a call to Reverse causes the sort order to be reversed.

  - The strings package has two new functions,
TrimPrefix
and
TrimSuffix
with self-evident properties, and the new method
Reader.WriteTo so the
Reader
type now implements the
io.WriterTo interface.

  - The syscall package’s
Fchflags function on various BSDs
(including Darwin) has changed signature.
It now takes an int as the first parameter instead of a string.
Since this API change fixes a bug, it is permitted by the Go 1 compatibility rules.

  - The syscall package also has received many updates
to make it more inclusive of constants and system calls for each supported operating system.

  - The testing package now automates the generation of allocation
statistics in tests and benchmarks using the new
AllocsPerRun function. And the
ReportAllocs
method on testing.B will enable printing of
memory allocation statistics for the calling benchmark. It also introduces the
AllocsPerOp method of
BenchmarkResult.
There is also a new
Verbose function to test the state of the -v
command-line flag,
and a new
Skip method of
testing.B and
testing.T
to simplify skipping an inappropriate test.

  - In the text/template
and
html/template packages,
templates can now use parentheses to group the elements of pipelines, simplifying the construction of complex pipelines.
Also, as part of the new parser, the
Node interface got two new methods to provide
better error reporting.
Although this violates the Go 1 compatibility rules,
no existing code should be affected because this interface is explicitly intended only to be used
by the
text/template
and
html/template
packages and there are safeguards to guarantee that.

  - The implementation of the unicode package has been updated to Unicode version 6.2.0.

  - In the unicode/utf8 package,
the new function ValidRune reports whether the rune is a valid Unicode code point.
To be valid, a rune must be in range and not be a surrogate half.
