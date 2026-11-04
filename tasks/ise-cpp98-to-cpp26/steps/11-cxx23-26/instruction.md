# Step 11 — the last two boundaries stop lying, and the dialect becomes C++26

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — it is compiled as C++20, its interface is delivered as
named modules, and a TCP session is one coroutine that awaits its own reads
and writes.

This is the last milestone of the line, and it is the smallest. Ten of them
have already been spent on the things that were structurally wrong: the
ownership, the threading, the vocabulary types, the compile-time dispatch, the
constraints, the module boundary, the protocol. What is left is two function
boundaries that still answer a question they cannot answer, and a dialect
number.

Both boundaries have the same shape and the same age. A function does
something that can fail. It knows *why* it failed — the reason is sitting in a
local variable one line above the `return` — and its return type is `bool`, so
the reason is dropped on the floor and the caller is told only that something
went wrong. In `ise_sys_utils.cpp` there are eight `std::error_code`
variables, every one of them handed to a `<filesystem>` call that fills it in
and every one of them discarded:

```c++
bool createDir(const string& dir)
{
    std::error_code ec;
    return fs::create_directory(fs::path(dir), ec) && !ec;   // ec dies here
}
```

A caller of `createDir()` cannot distinguish "the directory was already there"
from "you do not have permission to create it", and those call for opposite
responses. In `ise_inspector.cpp` the same information loss takes the other
classic form: `parseCommandUrl()` returns `bool` and fills three
out-parameters, so its caller declares three empty variables, hands them over,
and afterwards has to guess from one `bool` which of them were written.

C++23 has the type for this. `std::expected<T, E>` is a value that is either a
`T` or an `E`, it converts to `bool` exactly where the old return value did, so
the shape of every call site survives, and it carries the reason the old return
value threw away.

The dialect moves with it. The project is compiled as C++20 today;
`std::expected` and `<print>` are C++23, and the standard this migration
ends at is C++26, so this milestone sets the project's language standard to
26 and everything in the tree is compiled and built under it.

Be clear about what that last sentence is and is not worth. The new *language*
this milestone spends is C++23's: `std::expected`, `std::print` and
`std::flat_map`. C++26 is the dialect the build asks for, not a set of
features this codebase gets to use, and that is a deliberate limit rather than
an oversight. Of the facilities that would have justified the number,
`<linalg>`, `import std;`, reflection and a parallel `std::execution` backend
are not in this toolchain, as section 2.3 lists one by one, and a requirement
to use one of them would be a requirement to build something that cannot
compile here; `<mdspan>` is there, and this codebase holds no
multidimensional numeric data for it to view. So the milestone asks for the
three C++23 facilities the codebase has a real use for, and for the standard
setting itself, and does not pretend the rest is available or needed.

### What this milestone is not

* **It is not an error-handling rewrite.** `IseException` and the
  `iseThrowException` family stay exactly as they are. Nothing that throws
  today stops throwing, and nothing that returns a code today starts throwing.
  Two `bool`-returning boundaries gain a reason; that is the whole of it.
* **It is not a hunt for every `bool` in the codebase.** `fileExists()` and
  `directoryExists()` answer yes-or-no questions and `bool` is the right and
  complete answer to a yes-or-no question. `getFileSize()` keeps its
  documented `-1` convention and its `INT64` return type. `Buffer::
  loadFromFile()`, `FileStream::open()`, `HttpRequest::setRequestLine()`,
  `parseResponseHeader()` and `TimerQueue::getNearestExpiration()` keep the
  signatures they have. Requirement 2 names the seven functions it applies to
  and it applies to exactly those seven.
* **It is not an excuse to reach for everything C++23 and C++26 named.**
  Section 2 lists what this toolchain does and does not have. A feature that
  does not make one of the six requirements below true does not belong in
  this change.

### The six requirements

1. **The project is compiled as C++26.** `CMAKE_CXX_STANDARD` becomes `26`,
   `CMAKE_CXX_STANDARD_REQUIRED` stays `ON` and `CMAKE_CXX_EXTENSIONS` stays
   `OFF`, so every translation unit — the library's sources, its module
   interface units, and all fifteen examples — is compiled with `-std=c++26`
   and the project's existing warning set as errors -- `-Wall`, `-Wextra`,
   `-Woverloaded-virtual` and `-Werror`, carried on the targets, exactly as
   `CMakeLists.txt` spells them today. The build stays clean:
   no new diagnostic anywhere, and no warning switched off to get there.

2. **The filesystem boundary returns the error it already has.** These seven
   functions in `ise/main/ise_sys_utils.h` stop returning `bool`:

   | function | today | after |
   |---|---|---|
   | `createDir` | `bool` | `std::expected<void, std::error_code>` |
   | `deleteDir` | `bool` | `std::expected<void, std::error_code>` |
   | `forceDirectories` | `bool` | `std::expected<void, std::error_code>` |
   | `deleteFile` | `bool` | `std::expected<void, std::error_code>` |
   | `removeFile` | `bool` | `std::expected<void, std::error_code>` |
   | `renameFile` | `bool` | `std::expected<void, std::error_code>` |
   | `tryGetFileSize` | `std::optional<INT64>` | `std::expected<INT64, std::error_code>` |

   Introduce one named alias for the void case — the six functions that report
   only success or failure should say so in one type, not six spellings of it
   — and declare them with it.

   The error is not invented. Every Linux implementation of these seven
   already declares a `std::error_code`, passes it to the `<filesystem>` call
   that fills it, and then ignores it; that variable becomes the returned
   error. Where a `<filesystem>` call reports a refusal by returning `false`
   *without* setting the code — `create_directory()` on a path that is
   already a directory, `remove()` on a path that is not there — supply the
   `std::errc` that names what actually happened, so that no failure comes
   back with an empty error. A failing path hands the reason back as
   `std::unexpected(...)` carrying that code; a succeeding one returns the
   value, or nothing at all for the six that have no value to give. The `#ifdef ISE_WINDOWS` arms are not compiled
   here but they are part of the same declarations, so give them the same
   treatment from `GetLastError()` and `std::system_category()`.

   `getFileSize()` is not in the table: it keeps returning `INT64` and it
   keeps returning `-1` for a file it cannot size. It is one line of code on
   top of `tryGetFileSize()` and it stays one line.

3. **The callers act on the error instead of stepping around it.** Three
   call sites in the library use these functions today and all three ignore
   what they return.

   * `HttpClient::downloadFile()` in `ise/main/ise_http.cpp` calls
     `forceDirectories()` for the file's directory and then opens the file.
     A directory it could not create must be reported as
     `EC_HTTP_CANNOT_CREATE_FILE` at the point it is discovered, not three
     lines later as an `open()` that failed for no stated reason. A file name
     with no directory part is not a failure and must keep working.
   * The same function deletes the partial file when the transfer failed.
     That one *is* best effort — the caller is already being told the
     download failed and an unlinkable temporary cannot change that answer —
     so the result is discarded there rather than acted on — but the
     discard is written down, not left implicit. A cast to `void` says it
     in writing; a bare call statement does not.
     Put a comment beside it saying why this one is allowed to fail.
   * `Logger::writeToFile()` in `ise/main/ise_classes.cpp` calls
     `forceDirectories()` and then retries opening the log file. Retrying an
     open after a directory that was not created is a wasted syscall; the
     retry belongs behind a successful result.

4. **The inspector's URL parse returns one value.**
   `IseServerInspector::parseCommandUrl()` in `ise/main/ise_inspector.h`
   currently returns `bool` and writes into `string& category`, `string&
   command` and `PropertyList& argList`. Give it one parameter — the request
   — and one return value: an `std::expected` whose value is a small struct
   holding the three parsed pieces and whose error says what was wrong with
   the URL. `onHttpSession()`, its only caller, stops declaring three empty
   variables up front.

   Having the reason changes the answer that caller gives, and it should. A
   URL that does not parse is a malformed request, and a URL that parses but
   names no registered command is a request for something that is not here.
   Today both come back `404`. After this change the first is a `400` whose
   body includes the reason the parse carried, and the second stays `404 Not
   Found`.

   The reason goes out on the wire, so it is fixed text and not a choice. A
   URL that names a category and no command carries

       expected /category/command[?name=value&...]

   and the body that request gets back is `Bad Request: ` followed by those
   forty-three characters -- fifty-six bytes, and the `Content-Length` that
   counts them. No other byte of the response moves.

5. **Diagnostics are printed, not streamed.** The library and the examples
   write to standard output in forty places across fifteen files —
   thirty-eight `std::cout <<` chains and two `printf()`s in
   `ise/main/ise_application.cpp`. Every one of them goes through
   `<print>`: `std::println` where the line ended in `std::endl`, `std::print`
   where it did not. The text each one produces is unchanged, newline for
   newline.

   With the last `std::cout` gone, no source under `ise/` or `examples/`
   includes `<iostream>` any more — nineteen of them do today, including
   `ise/main/ise_sys_utils.h` and `ise/main/ise_classes.h`, which is why
   every program in the tree has been paying for iostreams whether it printed
   or not. Remove the include wherever it is now unused, in the headers, in
   the sources, and in the global module fragments of the interface units
   that mirror them, and add `<print>` where printing now happens.

6. **The server-module dispatch tables are flat maps.** `IseSvrModBusiness`
   in `ise/main/ise_svr_mod.h` keeps three tables — `actionCodeMap_`,
   `udpGroupIndexMap_` and `tcpServerIndexMap_`, declared through the
   protected typedefs `ActionCodeMap`, `UdpGroupIndexMap` and
   `TcpServerIndexMap` — that are filled once, while the server modules are
   being set up, and afterwards only looked up, once per packet or per
   connection event. A table that is built once and then only searched is
   the case C++23's `std::flat_map` exists for: sorted keys in contiguous
   storage instead of a tree of separately allocated nodes. After this
   milestone the three typedefs name `std::flat_map<UINT, int>` and the three
   members keep their names. `std::map` is not one of the spellings left for
   any of the three.

   Nothing a caller can observe moves with them. The same key finds the same
   value, a key that is not there is still not found, and a walk over a table
   still sees its keys in ascending order. `<flat_map>` is a standard header
   like `<print>`, so it follows the same module rule requirement 5 and
   section 2.4 describe.

## 2. What must not change

1. **The observable behaviour, with one exception, and the exception is
   requirement 4.** Every program still produces exactly the bytes it produces
   today: the same lines on standard output in the same order, the same log
   files with the same contents, the same protocol on the wire, the same exit
   codes. `createDir()` on an existing directory still reports failure to a
   caller that tests it as a `bool`; `deleteDir()` still refuses a path that
   is not a directory; `getFileSize()` still returns `-1` for a missing file;
   `forceDirectories()` on an empty string still fails. What changes is that a
   caller can now ask *why*, not what the answer is.

   The exception is the one place where having the reason changes what the
   answer should be. A request the inspector cannot parse — `GET /test`, which
   names a category and no command — answers `400 Bad Request` after this
   milestone instead of `404 Not Found`, and the body is the parse reason
   rather than `Not Found`. That is the only response on the wire that moves.
   A URL that parses and names no registered command is unchanged: still
   `404`, still `Not Found`. If you find yourself changing any other byte any
   program emits, you have gone further than this milestone asks.

   `std::expected` converts to `bool` explicitly, which is exactly what `if
   (f(x))` and `f(x) ? 1 : 0` do, so call sites that only tested the old
   `bool` keep compiling and keep meaning the same thing. Any call site that
   does not is a call site this milestone asked you to change.

2. **The rest of the public API.** Everything `ise/main/*.h` declares that
   this milestone does not name keeps its name, its signature, its default
   arguments and its semantics. That includes every other free function in
   `ise_sys_utils.h`, all of `ise_classes.h`, `ise_http.h`'s request,
   response and client classes, the timer queue, the thread pool, the server
   and connection classes and the coroutine types the session rewrite
   introduced.

3. **What this toolchain has, and what it does not.** Available under
   `-std=c++26`, verified in this image: `<expected>`, `<print>`, `<format>`,
   `<flat_map>`, `<flat_set>`, `<mdspan>`, `<span>`, `<ranges>`,
   `<concepts>`, `<coroutine>`, `<generator>`, `<syncstream>`. `<mdspan>` is
   there and is not asked for: nothing in this library is a multidimensional
   array. Not available, and not to be worked around: `<linalg>` is not in
   this standard library; `import std;` has no `std.cppm` to import; there is
   no reflection; `std::execution` has no parallel backend, so a parallel
   policy would run serially and buy nothing. `<stacktrace>` compiles but only links against `-lstdc++exp`, a library
   this project does not link, so it cannot be used here either. Nothing in this
   milestone needs any of them.

4. **Everything the earlier milestones established.** The tree you inherit
   is the output of ten milestones, and everything they established still
   holds. None of it is new work; it is here because this milestone retypes
   seven declarations, rewrites a parse and its caller and touches every
   place the tree prints, and a rule that was true of the code being replaced
   is true of the code that replaces it.

   * **The build.** The CMake floor, the target-scoped warning set, the
     `Threads::Threads` linkage and the explicit source lists, exactly as
     requirement 9 below spells them out. No source spells `throw()`; the
     destructors and the `what()` of the exception classes that carried it
     say `noexcept` instead.
   * **The module structure.** The library's declarations reach their
     consumers through named modules: the interface units are declared to the
     build as the library's own `PUBLIC` C++ module file set, every `.cpp`
     under `ise/main/` imports rather than includes a project header, the
     units import each other rather than re-declaring, and the declarations
     an interface unit exports stay attached to the global module so that the
     examples' textual includes still name the same entities. A header of
     yours under `ise/main/` is part of the library's interface and belongs
     to a named module reachable through `import ise;`, and a new `.cpp` in
     the library imports rather than includes. `ise/main/ise_options.h`,
     `ise_global_defs.h`, `ise_stldefs.h`, `ise_assert.h` and
     `ise_template.h` stay textual. A standard header first named inside a
     module purview attaches to that module and the compiler says so as an
     error, so a header this milestone adds to a public header belongs in the
     global module fragment of every interface unit that includes it.
   * **Ownership.** No delete-expression anywhere in the sources this
     environment compiles; every new-expression's result goes straight into
     an owning handle or into a `return` that hands ownership over; Boost
     owns nothing; run under the compiler's allocation checking, every
     program reaches exit with nothing still allocated and unreachable, and
     reports no use after free, no double release and no release of
     something never allocated. The ownership is spelled in the standard's
     words: the project's own objects are held by `std::unique_ptr` where
     ownership is exclusive and by `std::shared_ptr` where it is shared, and
     what the build produces still shows at least eight distinct `ise::`
     types held by `std::unique_ptr` and at least one held by
     `std::shared_ptr`.
   * **The language.** `nullptr` and no `NULL`; `override` on every
     override; scoped enumerations; `std::function` for a stored callback and
     a lambda — not a binder — at a call site that adapts one callable into
     another; range-based walks where a loop walks a container end to end,
     with `ise_timer.cpp`'s erasing loop excepted and left a loop; a move for
     every class that writes a copy; a pair taken apart by naming its halves
     rather than by `.first` and `.second`; and a lookup whose result is only
     meaningful inside the test it guards declared in that test.
   * **The concurrency.** No `volatile` declaration, no `__sync_*` or
     `__atomic_*`, and none of the `pthread_create`, `pthread_join`,
     `pthread_detach`, `pthread_exit`, `pthread_mutex_*`, `pthread_cond_*`,
     `pthread_attr_*`, `pthread_once*` or `PTHREAD_MUTEX_*` spellings;
     `std::recursive_mutex`, `std::condition_variable_any`, `std::call_once`,
     `std::thread`; `AtomicInteger<T>` storing a `std::atomic<T>` and taking
     no lock. Cancellation, the counting semaphore and the scheduling
     parameter have no standard word and are still the platform's, reached
     through the thread object's `native_handle()`: `pthread_cancel`,
     `pthread_setschedparam` and the `sem_*` family stay exactly where they
     are. The four `ThreadImpl` members one thread writes while another reads
     — `terminated_`, `isExecuting_`, `isSleepInterrupted_` and `threadId_` —
     are atomic objects; `run()` puts a `std::thread` into the member and
     `waitFor()` joins it; no thread function takes a `void *`; and elapsed
     time comes from a monotonic clock in `getCurTicks`, with the wall clock
     read only where the time itself is wanted (`Timestamp::now`, through
     `gettimeofday`) and no sleep built from a
     hand-filled `timespec`.
   * **The vocabulary.** `Context` is `std::any`, and no declaration in these
     sources is typed `std::variant` or `boost::variant`. The file and
     directory helpers are `<filesystem>`: a path is a
     `std::filesystem::path`, the `<filesystem>` calls report failure through
     a `std::error_code` rather than by throwing, the directory walk is the
     standard iterator, and the two questions the standard cannot answer are
     still asked of the kernel — exactly one access check and exactly one
     `stat`, with no other POSIX directory or file-entry call and no POSIX
     `rename`. The pool's task hand-out returns a `std::optional` instead of
     a `bool` and an out-parameter, and `getFileSize()` keeps answering from
     its sibling `tryGetFileSize()`, whose return type requirement 2 of
     section 1 sets. The twelve text and path helpers take
     `std::string_view`, and the parsers that hand their argument straight to
     a C function still take a terminated string — a view is never handed to
     something that needs a terminator.
   * **The compile-time work.** No function takes C's `...` except the
     private `ise::detail::formatStringRaw`; the packs are folded rather than
     forwarded; `intToStr` chooses its width in an `if constexpr`; the
     `[[nodiscard]]` attributes stay on the helpers that have them,
     `tryGetFileSize` among them; no source outside the frozen platform
     branches reads an argument back out of a `va_list` by hand; and no
     question in this project is answered by handing the work to a parallel
     execution policy.
   * **The constraints.** Every class template and every function template
     this project declares still states what it requires of its parameter as
     a constraint, the four concepts of `ise/main/ise_concepts.h` are
     unchanged, and no `static_assert` comes back anywhere in the
     tree.
   * **The library vocabulary of the C++20 milestones.** `Logger::writeFmt`
     takes a `std::format_string` and composes its message with
     `std::format`, not with `std::vformat` and not from a `const char *`;
     `DateTime`, `Timestamp` and `InetAddress` each default their `<=>` and
     their `==` rather than writing the comparisons out; and the eleven
     hand-written searches are ranges algorithms.
   * **A buffer carries its own length.** None of `TcpConnection::send`,
     `UdpCallbacks::classifyUdpPacket`, `UdpPacket::setPacketBuffer`,
     `UdpSocket::sendBuffer` or `MainUdpServer::onRecvData` takes a `void *`;
     the bytes a receive delivers come out of the awaiter `TcpConnection::recv`
     returns as one `std::span`; a buffer crossing the socket, server,
     server-module and application interfaces is a `std::span` rather than a
     pointer beside a length (`Buffer`, the streams, `Packet`, the packet
     splitters, `IoBuffer::append` and the HTTP client keep the
     pointer-and-length members they have); and
     `UdpPacket` hands out its memory through the one accessor `getPacket()`,
     with neither `getPacketBuffer` nor `getPacketSize` left in these
     sources.
   * **The sessions.** A TCP session is one coroutine: `TcpCallbacks`
     declares `onTcpConnection`, returning the detached `ConnectionTask`,
     beside `onTcpDisconnected`, and none of `onTcpConnected`,
     `onTcpRecvComplete` or `onTcpSendComplete` is declared, defined or
     called outside the frozen platform branches. `TcpConnection::send` and
     `TcpConnection::recv` return `[[nodiscard]]` awaiters declared inside
     `TcpConnection`; awaiting `send` yields whether the data went out, and
     the library's sessions and the examples' sessions `co_await` their
     transfers. The HTTP server declares no enumeration of its own, and
     `RecvReqState`, `SendResState`, `ConnContext` and `ConnContextPtr` do
     not exist; its session function is a coroutine, and the response body
     comes from the move-only, iterable `Generator<T>` declared beside
     `ConnectionTask`, yielded by a member function of `HttpServer` and
     walked with a range-`for`.

5. **The examples are still callers.** They keep `#include
   "ise/main/ise.h"`, gain no `import`, become no module, and link against the
   same library.

6. **The frozen platform branches.** The `#ifdef ISE_WINDOWS` arms are not
   compiled in this environment. Requirement 2 asks you to give the seven
   Windows implementations the same return type as their Linux siblings —
   they are the same declarations and they must compile if they were ever
   compiled — and beyond that the Windows arms, including
   `ise/main/ise_iocp.cpp` in its entirety, are not yours to edit.

7. **No diagnostic is suppressed.** No `#pragma` that disables a warning, no
   `-Wno-`, no `NOLINT`, no `-fpermissive`. `std::expected` is a `[[nodiscard]]`
   type, so ignoring one is a diagnostic; the fix is to use the result, and
   in the one place where discarding it is correct, to discard it in writing.

   Nothing presents a header of the project, or one the build
   generates, as a system header: no pragma, line marker, include
   option or build setting does that.

   The sanitizers are covered the same way. Nothing in the repository turns
   one off for any code, changes whether it stops at a report, sets its
   run-time options, calls into its run-time interface, or tests whether the
   build is instrumented. No such pragma, option, marker or sanitizer setting
   is written anywhere in the repository's sources, CMake files or scripts,
   not even in a comment that only mentions one.

8. **Nothing is passed to the compiler out of band.** No `-w`, no `--config`,
   no `-march=native`, no flags injected through the environment or a
   launcher. The only response file on any compile is the module map the
   generator produces, and what is inside it counts exactly as if it were
    spelled on the command line.

9. **The build.** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake
   --build build`, from a clean tree, configures and builds the library and
   all fifteen examples with no diagnostics. `libise.a` in the same place, the
   same fifteen executables in the same places with the same names.

   The rules that build already follows are still in force, and are written
   out here rather than left to memory:

   * `cmake_minimum_required` names `VERSION 3.28` or later, and no CMake
     file anywhere in the project declares a minimum below that.
   * None of the directory-scoped and retired commands appear anywhere:
     `include_directories`, `link_directories`, `add_definitions`,
     `add_compile_options`, `add_compile_definitions`, `add_link_options`,
     `link_libraries`, `exec_program`, `build_name`, `remove`,
     `subdir_depends`, `output_required_files`, `use_mangled_mesa`,
     `utility_source`, `variable_requires`, `install_files`,
     `install_programs`, `install_targets`. Include paths, definitions,
     options and link dependencies are properties of targets and are attached
     to targets. This is a property of the project as CMake configures it, not
     of how its text is spelled: assembling one of these names at configure
     time, or reaching one through a variable that expands to it, is writing
     it.
   * Source lists stay written down. Neither `aux_source_directory` nor
     `file(GLOB ...)` decides at configure time what the project is made of.
     A file this milestone adds to the project is a file you add to the list,
     and a file it empties is one you remove from the list.
   * Every `target_link_libraries` call uses the keyword form -- the target
     name followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any
     dependency -- and no link line names `pthread`, `dl`, `rt` or
     `stdc++fs`; `-lpthread`, `-ldl`, `-lrt` and `${CMAKE_DL_LIBS}`, which is
     CMake's own spelling of `dl`, appear nowhere in the project's CMake
     files. Threading comes from `find_package(Threads REQUIRED)` and the
     imported target it provides.
   * The project chooses neither a compiler nor a machine. No
     `set(CMAKE_C_COMPILER ...)` or `set(CMAKE_CXX_COMPILER ...)`, no
     `-march=native`, and no assignment to `CMAKE_C_FLAGS`,
     `CMAKE_CXX_FLAGS`, their per-configuration forms such as
     `CMAKE_CXX_FLAGS_DEBUG`, or the `_INIT` variables they are seeded from.
     Nothing is interposed between CMake and that compiler either: no
     `CMAKE_CXX_COMPILER_LAUNCHER`, no `CXX_COMPILER_LAUNCHER` target
     property, and neither `RULE_LAUNCH_COMPILE` nor `RULE_LAUNCH_LINK` on any
     scope that carries properties.
   * Compile options, including the warning set above, are carried on targets
     through `target_compile_options` rather than pushed into a global flags
     variable.
   * The hand-written Linux build under `build/linux/` stays gone rather than
     reappearing beside the one CMake drives.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_sys_utils.h`** — the seven declarations of requirement 2,
  and the `[[nodiscard]]` style the rest of the header already uses.
- **`ise/main/ise_sys_utils.cpp`** — the eight `std::error_code` variables
  that are filled and dropped, and the Windows arms beside them.
- **`ise/main/ise_http.cpp`** — `HttpClient::downloadFile()`, two of
  requirement 3's three call sites.
- **`ise/main/ise_classes.cpp`** — `Logger::writeToFile()`, the third.
- **`ise/main/ise_inspector.h`** and **`ise_inspector.cpp`** —
  `parseCommandUrl()` and `onHttpSession()`, requirement 4.
- **`ise/main/ise_application.cpp`** — the library's only two `printf()`s and
  its one `std::cout` chain.
- **`examples/`** — fourteen of the fifteen print, all through the same two
  or three lines near the top of each program.
- **`ise/main/ise.*.cppm`** — the global module fragments that mirror the
  headers, and that requirement 5 and section 2's rule 4 both reach.
- **`CMakeLists.txt`** — `CMAKE_CXX_STANDARD`, the `ise` target and its
  `FILE_SET CXX_MODULES`.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of section 2
is defined by the code as it stands, so read the code.

## 4. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests and documents. Do not create a prose answer or an alternate entry
  point: the deliverable is the tree, building and running.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system for
  other implementations or answers.
- Leave a reproducible handoff for the next fresh session: a tree that
  a new session can configure and build with the two commands in requirement
  9 of section 2, and notes on anything you changed in the sources and why.
