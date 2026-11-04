# Go 1.5 Release Notes

## Introduction to Go 1.5

The latest Go release, version 1.5,
is a significant release, including major architectural changes to the implementation.
Despite that, we expect almost all Go programs to continue to compile and run as before,
because the release still maintains the Go 1 promise
of compatibility.

The biggest developments in the implementation are:

  - The compiler and runtime are now written entirely in Go (with a little assembler).
C is no longer involved in the implementation, and so the C compiler that was
once necessary for building the distribution is gone.

  - The garbage collector is now concurrent and provides dramatically lower
pause times by running, when possible, in parallel with other goroutines.

  - By default, Go programs run with GOMAXPROCS set to the
number of cores available; in prior releases it defaulted to 1.

  - Support for internal packages
is now provided for all repositories, not just the Go core.

  - The go command now provides experimental
support for “vendoring” external dependencies.

  - A new go tool trace command supports fine-grained
tracing of program execution.

  - A new go doc command (distinct from godoc)
is customized for command-line use.

These and a number of other changes to the implementation and tools
are discussed below.

The release also contains one small language change involving map literals.

Finally, the timing of the release
strays from the usual six-month interval,
both to provide more time to prepare this major release and to shift the schedule thereafter to
time the release dates more conveniently.

## Changes to the language

### Map literals

Due to an oversight, the rule that allowed the element type to be elided from slice literals was not
applied to map keys.
This has been corrected in Go 1.5.
An example will make this clear.
As of Go 1.5, this map literal,

```
m := map[Point]string{
    Point{29.935523, 52.891566}:   "Persepolis",
    Point{-25.352594, 131.034361}: "Uluru",
    Point{37.422455, -122.084306}: "Googleplex",
}

may be written as follows, without the Point type listed explicitly:

```
m := map[Point]string{
    {29.935523, 52.891566}:   "Persepolis",
    {-25.352594, 131.034361}: "Uluru",
    {37.422455, -122.084306}: "Googleplex",
}

## The Implementation

### No more C

The compiler and runtime are now implemented in Go and assembler, without C.
The only C source left in the tree is related to testing or to cgo.
There was a C compiler in the tree in 1.4 and earlier.
It was used to build the runtime; a custom compiler was necessary in part to
guarantee the C code would work with the stack management of goroutines.
Since the runtime is in Go now, there is no need for this C compiler and it is gone.
Details of the process to eliminate C are discussed elsewhere.

The conversion from C was done with the help of custom tools created for the job.
Most important, the compiler was actually moved by automatic translation of
the C code into Go.
It is in effect the same program in a different language.
It is not a new implementation
of the compiler so we expect the process will not have introduced new compiler
bugs.
An overview of this process is available in the slides for
this presentation.

### Compiler and tools

Independent of but encouraged by the move to Go, the names of the tools have changed.
The old names 6g, 8g and so on are gone; instead there
is just one binary, accessible as go tool compile,
that compiles Go source into binaries suitable for the architecture and operating system
specified by $GOARCH and $GOOS.
Similarly, there is now one linker (go tool link)
and one assembler (go tool asm).
The linker was translated automatically from the old C implementation,
but the assembler is a new native Go implementation discussed
in more detail below.

Similar to the drop of the names 6g, 8g, and so on,
the output of the compiler and assembler are now given a plain .o suffix
rather than .8, .6, etc.

### Garbage collector

The garbage collector has been re-engineered for 1.5 as part of the development
outlined in the design document.
Expected latencies are much lower than with the collector
in prior releases, through a combination of advanced algorithms,
better scheduling of the collector,
and running more of the collection in parallel with the user program.
The “stop the world” phase of the collector
will almost always be under 10 milliseconds and usually much less.

For systems that benefit from low latency, such as user-responsive web sites,
the drop in expected latency with the new collector may be important.

Details of the new collector were presented in a
talk at GopherCon 2015.

### Runtime

In Go 1.5, the order in which goroutines are scheduled has been changed.
The properties of the scheduler were never defined by the language,
but programs that depend on the scheduling order may be broken
by this change.
We have seen a few (erroneous) programs affected by this change.
If you have programs that implicitly depend on the scheduling
order, you will need to update them.

Another potentially breaking change is that the runtime now
sets the default number of threads to run simultaneously,
defined by GOMAXPROCS, to the number
of cores available on the CPU.
In prior releases the default was 1.
Programs that do not expect to run with multiple cores may
break inadvertently.
They can be updated by removing the restriction or by setting
GOMAXPROCS explicitly.
For a more detailed discussion of this change, see
the design document.

### Build

Now that the Go compiler and runtime are implemented in Go, a Go compiler
must be available to compile the distribution from source.
Thus, to build the Go core, a working Go distribution must already be in place.
(Go programmers who do not work on the core are unaffected by this change.)
Any Go 1.4 or later distribution (including gccgo) will serve.
For details, see the design document.

## Ports

Due mostly to the industry’s move away from the 32-bit x86 architecture,
the set of binary downloads provided is reduced in 1.5.
A distribution for the OS X operating system is provided only for the
amd64 architecture, not 386.
Similarly, the ports for Snow Leopard (Apple OS X 10.6) still work but are no
longer released as a download or maintained since Apple no longer maintains that version
of the operating system.
Also, the dragonfly/386 port is no longer supported at all
because DragonflyBSD itself no longer supports the 32-bit 386 architecture.

There are however several new ports available to be built from source.
These include darwin/arm and darwin/arm64.
The new port linux/arm64 is mostly in place, but cgo
is only supported using external linking.

Also available as experiments are ppc64
and ppc64le (64-bit PowerPC, big- and little-endian).
Both these ports support cgo but
only with internal linking.

On FreeBSD, Go 1.5 requires FreeBSD 8-STABLE+ because of its new use of the SYSCALL instruction.

On NaCl, Go 1.5 requires SDK version pepper-41. Later pepper versions are not
compatible due to the removal of the sRPC subsystem from the NaCl runtime.

On Darwin, the use of the system X.509 certificate interface can be disabled
with the ios build tag.

The Solaris port now has full support for cgo and the packages
net and
crypto/x509,
as well as a number of other fixes and improvements.

## Tools

### Translating

As part of the process to eliminate C from the tree, the compiler and
linker were translated from C to Go.
It was a genuine (machine assisted) translation, so the new programs are essentially
the old programs translated rather than new ones with new bugs.
We are confident the translation process has introduced few if any new bugs,
and in fact uncovered a number of previously unknown bugs, now fixed.

The assembler is a new program, however; it is described below.

### Renaming

The suites of programs that were the compilers (6g, 8g, etc.),
the assemblers (6a, 8a, etc.),
and the linkers (6l, 8l, etc.)
have each been consolidated into a single tool that is configured
by the environment variables GOOS and GOARCH.
The old names are gone; the new tools are available through the go tool
mechanism as go tool compile,
go tool asm,
and go tool link.
Also, the file suffixes .6, .8, etc. for the
intermediate object files are also gone; now they are just plain .o files.

For example, to build and link a program on amd64 for Darwin
using the tools directly, rather than through go build,
one would run:

```
$ export GOOS=darwin GOARCH=amd64
$ go tool compile program.go
$ go tool link program.o

### Moving

Because the go/types package
has now moved into the main repository (see below),
the vet and
cover
tools have also been moved.
They are no longer maintained in the external golang.org/x/tools repository,
although (deprecated) source still resides there for compatibility with old releases.

### Compiler

As described above, the compiler in Go 1.5 is a single Go program,
translated from the old C source, that replaces 6g, 8g,
and so on.
Its target is configured by the environment variables GOOS and GOARCH.

The 1.5 compiler is mostly equivalent to the old,
but some internal details have changed.
One significant change is that evaluation of constants now uses
the math/big package
rather than a custom (and less well tested) implementation of high precision
arithmetic.
We do not expect this to affect the results.

For the amd64 architecture only, the compiler has a new option, -dynlink,
that assists dynamic linking by supporting references to Go symbols
defined in external shared libraries.

### Assembler

Like the compiler and linker, the assembler in Go 1.5 is a single program
that replaces the suite of assemblers (6a,
8a, etc.) and the environment variables
GOARCH and GOOS
configure the architecture and operating system.
Unlike the other programs, the assembler is a wholly new program
written in Go.

The new assembler is very nearly compatible with the previous
ones, but there are a few changes that may affect some
assembler source files.
See the updated assembler guide
for more specific information about these changes. In summary:

First, the expression evaluation used for constants is a little
different.
It now uses unsigned 64-bit arithmetic and the precedence
of operators (+, -, <<, etc.)
comes from Go, not C.
We expect these changes to affect very few programs but
manual verification may be required.

Perhaps more important is that on machines where
SP or PC is only an alias
for a numbered register,
such as R13 for the stack pointer and
R15 for the hardware program counter
on ARM,
a reference to such a register that does not include a symbol
is now illegal.
For example, SP and 4(SP) are
illegal but sym+4(SP) is fine.
On such machines, to refer to the hardware register use its
true R name.

One minor change is that some of the old assemblers
permitted the notation

```
constant=value

to define a named constant.
Since this is always possible to do with the traditional
C-like #define notation, which is still
supported (the assembler includes an implementation
of a simplified C preprocessor), the feature was removed.

### Linker

The linker in Go 1.5 is now one Go program,
that replaces 6l, 8l, etc.
Its operating system and instruction set are specified
by the environment variables GOOS and GOARCH.

There are several other changes.
The most significant is the addition of a -buildmode option that
expands the style of linking; it now supports
situations such as building shared libraries and allowing other languages
to call into Go libraries.
Some of these were outlined in a design document.
For a list of the available build modes and their use, run

```
$ go help buildmode

Another minor change is that the linker no longer records build time stamps in
the header of Windows executables.
Also, although this may be fixed, Windows cgo executables are missing some
DWARF information.

Finally, the -X flag, which takes two arguments,
as in

```
-X importpath.name value

now also accepts a more common Go flag style with a single argument
that is itself a name=value pair:

```
-X importpath.name=value

Although the old syntax still works, it is recommended that uses of this
flag in scripts and the like be updated to the new form.

### Go command

The go command’s basic operation
is unchanged, but there are a number of changes worth noting.

The previous release introduced the idea of a directory internal to a package
being unimportable through the go command.
In 1.4, it was tested with the introduction of some internal elements
in the core repository.
As suggested in the design document,
that change is now being made available to all repositories.
The rules are explained in the design document, but in summary any
package in or under a directory named internal may
be imported by packages rooted in the same subtree.
Existing packages with directory elements named internal may be
inadvertently broken by this change, which was why it was advertised
in the last release.

Another change in how packages are handled is the experimental
addition of support for “vendoring”.
For details, see the documentation for the go command
and the design document.

There have also been several minor changes.
Read the documentation for full details.

  - SWIG support has been updated such that
.swig and .swigcxx
now require SWIG 3.0.6 or later.

  - The install subcommand now removes the
binary created by the build subcommand
in the source directory, if present,
to avoid problems having two binaries present in the tree.

  - The std (standard library) wildcard package name
now excludes commands.
A new cmd wildcard covers the commands.

  - A new -asmflags build option
sets flags to pass to the assembler.
However,
the -ccflags build option has been dropped;
it was specific to the old, now deleted C compiler .

  - A new -buildmode build option
sets the build mode, described above.

  - A new -pkgdir build option
sets the location of installed package archives,
to help isolate custom builds.

  - A new -toolexec build option
allows substitution of a different command to invoke
the compiler and so on.
This acts as a custom replacement for go tool.

  - The test subcommand now has a -count
flag to specify how many times to run each test and benchmark.
The testing package
does the work here, through the -test.count flag.

  - The generate subcommand has a couple of new features.
The -run option specifies a regular expression to select which directives
to execute; this was proposed but never implemented in 1.4.
The executing pattern now has access to two new environment variables:
$GOLINE returns the source line number of the directive
and $DOLLAR expands to a dollar sign.

  - The get subcommand now has a -insecure
flag that must be enabled if fetching from an insecure repository, one that
does not encrypt the connection.

### Go vet command

The go tool vet command now does
more thorough validation of struct tags.

### Trace command

A new tool is available for dynamic execution tracing of Go programs.
The usage is analogous to how the test coverage tool works.
Generation of traces is integrated into go test,
and then a separate execution of the tracing tool itself analyzes the results:

```
$ go test -trace=trace.out path/to/package
$ go tool trace [flags] pkg.test trace.out

The flags enable the output to be displayed in a browser window.
For details, run go tool trace -help.
There is also a description of the tracing facility in this
talk
from GopherCon 2015.

### Go doc command

A few releases back, the go doc
command was deleted as being unnecessary.
One could always run “godoc .” instead.
The 1.5 release introduces a new go doc
command with a more convenient command-line interface than
godoc’s.
It is designed for command-line usage specifically, and provides a more
compact and focused presentation of the documentation for a package
or its elements, according to the invocation.
It also provides case-insensitive matching and
support for showing the documentation for unexported symbols.
For details run “go help doc”.

### Cgo

When parsing #cgo lines,
the invocation ${SRCDIR} is now
expanded into the path to the source directory.
This allows options to be passed to the
compiler and linker that involve file paths relative to the
source code directory. Without the expansion the paths would be
invalid when the current working directory changes.

Solaris now has full cgo support.

On Windows, cgo now uses external linking by default.

When a C struct ends with a zero-sized field, but the struct itself is
not zero-sized, Go code can no longer refer to the zero-sized field.
Any such references will have to be rewritten.

## Performance

As always, the changes are so general and varied that precise statements
about performance are difficult to make.
The changes are even broader ranging than usual in this release, which
includes a new garbage collector and a conversion of the runtime to Go.
Some programs may run faster, some slower.
On average the programs in the Go 1 benchmark suite run a few percent faster in Go 1.5
than they did in Go 1.4,
while as mentioned above the garbage collector’s pauses are
dramatically shorter, and almost always under 10 milliseconds.

Builds in Go 1.5 will be slower by a factor of about two.
The automatic translation of the compiler and linker from C to Go resulted in
unidiomatic Go code that performs poorly compared to well-written Go.
Analysis tools and refactoring helped to improve the code, but much remains to be done.
Further profiling and optimization will continue in Go 1.6 and future releases.
For more details, see these slides
and associated video.

## Standard library

### Flag

The flag package’s
PrintDefaults
function, and method on FlagSet,
have been modified to create nicer usage messages.
The format has been changed to be more human-friendly and in the usage
messages a word quoted with `backquotes` is taken to be the name of the
flag’s operand to display in the usage message.
For instance, a flag created with the invocation,

```
cpuFlag = flag.Int("cpu", 1, "run `N` processes in parallel")

will show the help message,

```
-cpu N
        run N processes in parallel (default 1)

Also, the default is now listed only when it is not the zero value for the type.

### Floats in math/big

The math/big package
has a new, fundamental data type,
Float,
which implements arbitrary-precision floating-point numbers.
A Float value is represented by a boolean sign,
a variable-length mantissa, and a 32-bit fixed-size signed exponent.
The precision of a Float (the mantissa size in bits)
can be specified explicitly or is otherwise determined by the first
operation that creates the value.
Once created, the size of a Float’s mantissa may be modified with the
SetPrec method.
Floats support the concept of infinities, such as are created by
overflow, but values that would lead to the equivalent of IEEE 754 NaNs
trigger a panic.
Float operations support all IEEE-754 rounding modes.
When the precision is set to 24 (53) bits,
operations that stay within the range of normalized float32
(float64)
values produce the same results as the corresponding IEEE-754
arithmetic on those values.

### Go types

The go/types package
up to now has been maintained in the golang.org/x
repository; as of Go 1.5 it has been relocated to the main repository.
The code at the old location is now deprecated.
There is also a modest API change in the package, discussed below.

Associated with this move, the
go/constant
package also moved to the main repository;
it was golang.org/x/tools/exact before.
The go/importer package
also moved to the main repository,
as well as some tools described above.

### Net

The DNS resolver in the net package has almost always used cgo to access
the system interface.
A change in Go 1.5 means that on most Unix systems DNS resolution
will no longer require cgo, which simplifies execution
on those platforms.
Now, if the system’s networking configuration permits, the native Go resolver
will suffice.
The important effect of this change is that each DNS resolution occupies a goroutine
rather than a thread,
so a program with multiple outstanding DNS requests will consume fewer operating
system resources.

The decision of how to run the resolver applies at run time, not build time.
The netgo build tag that has been used to enforce the use
of the Go resolver is no longer necessary, although it still works.
A new netcgo build tag forces the use of the cgo resolver at
build time.
To force cgo resolution at run time set
GODEBUG=netdns=cgo in the environment.
More debug options are documented here.

This change applies to Unix systems only.
Windows, Mac OS X, and Plan 9 systems behave as before.

### Reflect

The reflect package
has two new functions: ArrayOf
and FuncOf.
These functions, analogous to the extant
SliceOf function,
create new types at runtime to describe arrays and functions.

### Hardening

Several dozen bugs were found in the standard library
through randomized testing with the
go-fuzz tool.
Bugs were fixed in the
archive/tar,
archive/zip,
compress/flate,
encoding/gob,
fmt,
html/template,
image/gif,
image/jpeg,
image/png, and
text/template,
packages.
The fixes harden the implementation against incorrect and malicious inputs.

### Minor changes to the library

  - The archive/zip package’s
Writer type now has a
SetOffset
method to specify the location within the output stream at which to write the archive.

  - The Reader in the
bufio package now has a
Discard
method to discard data from the input.

  - In the bytes package,
the Buffer type
now has a Cap method
that reports the number of bytes allocated within the buffer.
Similarly, in both the bytes
and strings packages,
the Reader
type now has a Size
method that reports the original length of the underlying slice or string.

  - Both the bytes and
strings packages
also now have a LastIndexByte
function that locates the rightmost byte with that value in the argument.

  - The crypto package
has a new interface, Decrypter,
that abstracts the behavior of a private key used in asymmetric decryption.

  - In the crypto/cipher package,
the documentation for the Stream
interface has been clarified regarding the behavior when the source and destination are
different lengths.
If the destination is shorter than the source, the method will panic.
This is not a change in the implementation, only the documentation.

  - Also in the crypto/cipher package,
there is now support for nonce lengths other than 96 bytes in AES’s Galois/Counter mode (GCM),
which some protocols require.

  - In the crypto/elliptic package,
there is now a Name field in the
CurveParams struct,
and the curves implemented in the package have been given names.
These names provide a safer way to select a curve, as opposed to
selecting its bit size, for cryptographic systems that are curve-dependent.

  - Also in the crypto/elliptic package,
the Unmarshal function
now verifies that the point is actually on the curve.
(If it is not, the function returns nils).
This change guards against certain attacks.

  - The crypto/sha512
package now has support for the two truncated versions of
the SHA-512 hash algorithm, SHA-512/224 and SHA-512/256.

  - The crypto/tls package
minimum protocol version now defaults to TLS 1.0.
The old default, SSLv3, is still available through Config if needed.

  - The crypto/tls package
now supports Signed Certificate Timestamps (SCTs) as specified in RFC 6962.
The server serves them if they are listed in the
Certificate struct,
and the client requests them and exposes them, if present,
in its ConnectionState struct.

  - The stapled OCSP response to a crypto/tls client connection,
previously only available via the
OCSPResponse method,
is now exposed in the ConnectionState struct.

  - The crypto/tls server implementation
will now always call the
GetCertificate function in
the Config struct
to select a certificate for the connection when none is supplied.

  - Finally, the session ticket keys in the
crypto/tls package
can now be changed while the server is running.
This is done through the new
SetSessionTicketKeys
method of the
Config type.

  - In the crypto/x509 package,
wildcards are now accepted only in the leftmost label as defined in
the specification.

  - Also in the crypto/x509 package,
the handling of unknown critical extensions has been changed.
They used to cause parse errors but now they are parsed and caused errors only
in Verify.
The new field UnhandledCriticalExtensions of
Certificate records these extensions.

  - The DB type of the
database/sql package
now has a Stats method
to retrieve database statistics.

  - The debug/dwarf
package has extensive additions to better support DWARF version 4.
See for example the definition of the new type
Class.

  - The debug/dwarf package
also now supports decoding of DWARF line tables.

  - The debug/elf
package now has support for the 64-bit PowerPC architecture.

  - The encoding/base64 package
now supports unpadded encodings through two new encoding variables,
RawStdEncoding and
RawURLEncoding.

  - The encoding/json package
now returns an UnmarshalTypeError
if a JSON value is not appropriate for the target variable or component
to which it is being unmarshaled.

  - The encoding/json’s
Decoder
type has a new method that provides a streaming interface for decoding
a JSON document:
Token.
It also interoperates with the existing functionality of Decode,
which will continue a decode operation already started with Decoder.Token.

  - The flag package
has a new function, UnquoteUsage,
to assist in the creation of usage messages using the new convention
described above.

  - In the fmt package,
a value of type Value now
prints what it holds, rather than use the reflect.Value’s Stringer
method, which produces things like <int Value>.

  - The EmptyStmt type
in the go/ast package now
has a boolean Implicit field that records whether the
semicolon was implicitly added or was present in the source.

  - For forward compatibility the go/build package
reserves GOARCH values for a number of architectures that Go might support one day.
This is not a promise that it will.
Also, the Package struct
now has a PkgTargetRoot field that stores the
architecture-dependent root directory in which to install, if known.

  - The (newly migrated) go/types
package allows one to control the prefix attached to package-level names using
the new Qualifier
function type as an argument to several functions. This is an API change for
the package, but since it is new to the core, it is not breaking the Go 1 compatibility
rules since code that uses the package must explicitly ask for it at its new location.
To update, run
go fix on your package.

  - In the image package,
the Rectangle type
now implements the Image interface,
so a Rectangle can serve as a mask when drawing.

  - Also in the image package,
to assist in the handling of some JPEG images,
there is now support for 4:1:1 and 4:1:0 YCbCr subsampling and basic
CMYK support, represented by the new image.CMYK struct.

  - The image/color package
adds basic CMYK support, through the new
CMYK struct,
the CMYKModel color model, and the
CMYKToRGB function, as
needed by some JPEG images.

  - Also in the image/color package,
the conversion of a YCbCr
value to RGBA has become more precise.
Previously, the low 8 bits were just an echo of the high 8 bits;
now they contain more accurate information.
Because of the echo property of the old code, the operation
uint8(r) to extract an 8-bit red value worked, but is incorrect.
In Go 1.5, that operation may yield a different value.
The correct code is, and always was, to select the high 8 bits:
uint8(r>>8).
Incidentally, the image/draw package
provides better support for such conversions; see
this blog post
for more information.

  - Finally, as of Go 1.5 the closest match check in
Index
now honors the alpha channel.

  - The image/gif package
includes a couple of generalizations.
A multiple-frame GIF file can now have an overall bounds different
from all the contained single frames’ bounds.
Also, the GIF struct
now has a Disposal field
that specifies the disposal method for each frame.

  - The io package
adds a CopyBuffer function
that is like Copy but
uses a caller-provided buffer, permitting control of allocation and buffer size.

  - The log package
has a new LUTC flag
that causes time stamps to be printed in the UTC time zone.
It also adds a SetOutput method
for user-created loggers.

  - In Go 1.4, Max was not detecting all possible NaN bit patterns.
This is fixed in Go 1.5, so programs that use math.Max on data including NaNs may behave differently,
but now correctly according to the IEEE754 definition of NaNs.

  - The math/big package
adds a new Jacobi
function for integers and a new
ModSqrt
method for the Int type.

  - The mime package
adds a new WordDecoder type
to decode MIME headers containing RFC 204-encoded words.
It also provides BEncoding and
QEncoding
as implementations of the encoding schemes of RFC 2045 and RFC 2047.

  - The mime package also adds an
ExtensionsByType
function that returns the MIME extensions know to be associated with a given MIME type.

  - There is a new mime/quotedprintable
package that implements the quoted-printable encoding defined by RFC 2045.

  - The net package will now
Dial hostnames by trying each
IP address in order until one succeeds.
The Dialer.DualStack
mode now implements Happy Eyeballs
(RFC 6555) by giving the
first address family a 300ms head start; this value can be overridden by
the new Dialer.FallbackDelay.

  - A number of inconsistencies in the types returned by errors in the
net package have been
tidied up.
Most now return an
OpError value
with more information than before.
Also, the OpError
type now includes a Source field that holds the local
network address.

  - The net/http package now
has support for setting trailers from a server Handler.
For details, see the documentation for
ResponseWriter.

  - There is a new method to cancel a net/http
Request by setting the new
Request.Cancel
field.
It is supported by http.Transport.
The Cancel field’s type is compatible with the
context.Context.Done
return value.

  - Also in the net/http package,
there is code to ignore the zero Time value
in the ServeContent function.
As of Go 1.5, it now also ignores a time value equal to the Unix epoch.

  - The net/http/fcgi package
exports two new errors,
ErrConnClosed and
ErrRequestAborted,
to report the corresponding error conditions.

  - The net/http/cgi package
had a bug that mishandled the values of the environment variables
REMOTE_ADDR and REMOTE_HOST.
This has been fixed.
Also, starting with Go 1.5 the package sets the REMOTE_PORT
variable.

  - The net/mail package
adds an AddressParser
type that can parse mail addresses.

  - The net/smtp package
now has a TLSConnectionState
accessor to the Client
type that returns the client’s TLS state.

  - The os package
has a new LookupEnv function
that is similar to Getenv
but can distinguish between an empty environment variable and a missing one.

  - The os/signal package
adds new Ignore and
Reset functions.

  - The runtime,
runtime/trace,
and net/http/pprof packages
each have new functions to support the tracing facilities described above:
ReadTrace,
StartTrace,
StopTrace,
Start,
Stop, and
Trace.
See the respective documentation for details.

  - The runtime/pprof package
by default now includes overall memory statistics in all memory profiles.

  - The strings package
has a new Compare function.
This is present to provide symmetry with the bytes package
but is otherwise unnecessary as strings support comparison natively.

  - The WaitGroup implementation in
package sync
now diagnoses code that races a call to Add
against a return from Wait.
If it detects this condition, the implementation panics.

  - In the syscall package,
the Linux SysProcAttr struct now has a
GidMappingsEnableSetgroups field, made necessary
by security changes in Linux 3.19.
On all Unix systems, the struct also has new Foreground and Pgid fields
to provide more control when exec’ing.
On Darwin, there is now a Syscall9 function
to support calls with too many arguments.

  - The testing/quick will now
generate nil values for pointer types,
making it possible to use with recursive data structures.
Also, the package now supports generation of array types.

  - In the text/template and
html/template packages,
integer constants too large to be represented as a Go integer now trigger a
parse error. Before, they were silently converted to floating point, losing
precision.

  - Also in the text/template and
html/template packages,
a new Option method
allows customization of the behavior of the template during execution.
The sole implemented option allows control over how a missing key is
handled when indexing a map.
The default, which can now be overridden, is as before: to continue with an invalid value.

  - The time package’s
Time type has a new method
AppendFormat,
which can be used to avoid allocation when printing a time value.

  - The unicode package and associated
support throughout the system has been upgraded from version 7.0 to
Unicode 8.0.
