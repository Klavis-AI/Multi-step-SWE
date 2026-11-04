# Step 8 — a buffer that knows how long it is

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — and it is compiled as C++20 already, with the project's
own warning set promoted to errors.

The last milestone finished the language half of C++20: what this library's
templates require of their parameters is now written on the declarations, as
constraints the compiler reads before the call is a call. It changed no
signature and no behaviour.

This milestone is the library half, and it does change signatures. C++20's
standard library answers four questions this codebase has been answering by
hand since it was written in C++98, and answering the same way every time:

```
how a message is composed          sprintf, and the project's own formatString
what ordering a value type has     six relational operators, written out
how a buffer crosses an interface  a void* and an int, side by side
how a container is searched        a loop over an index with a break in it
```

Each of those is a place where the code says *how* and leaves *what* to the
reader. `std::format`, `operator<=>`, `std::span` and the ranges algorithms
say what.

There is a fifth thing in C++20's library — `<ranges>`' lazy views used as a
pipeline, `std::jthread`, `std::barrier`, `std::latch`, the calendar in
`<chrono>`, `std::source_location` — and none of it is in this milestone. The
four above are the ones this codebase has a use for today.

### The four facilities

1. **Every message this project composes at run time is composed with
   `std::format`.** There are two printf-shaped call sites in the tree today
   and one printf-shaped helper behind them:

   * `sprintf` into a fixed stack buffer: one in `ise/main/ise_sys_utils.cpp`
     (`floatToStr`, into 256 bytes) and one in `ise/main/ise_socket.cpp`
     (`ipToString`, into 64). Neither knows how long its buffer is; `sprintf`
     does not take a length.
   * `ise::formatString(const char *format, Args... args)`, declared in
     `ise/main/ise_sys_utils.h` — the project's own printf. It is already a
     variadic template, and its parameter pack is already constrained by a
     concept declared in `ise/main/ise_concepts.h`. About sixty call sites go
     through it.

   After this milestone:

   * **No source this environment compiles calls `sprintf`.** Where a byte
     count is genuinely what is wanted — the two-pass length probe inside
     `ise_sys_utils.cpp`'s own `floatToStr` and `formatStringV` — the call is
     `std::snprintf`/`std::vsnprintf` with the size passed, and the first pass
     asks for the length rather than guessing it.
   * **The call sites that compose a message compose it with `std::format`.**
     There are more than thirty of them: every `logger().writeStr(...)` that
     builds a string, every exception message, every line the HTTP and
     inspector code assembles, and the string helpers in `ise_sys_utils.cpp`.
   * **`ise::formatString` itself stays**, declared where it is, and so does
     `formatStringV` with its `va_list`. They are public API, and the
     variadic template in `ise/main/ise_sys_utils.h` is already type-safe.
     What changes is that the project stops reaching for them by default.

   Two groups of calls to `formatString` stay, deliberately, and are the
   reason the requirement is "the call sites that compose a message" and not
   "all of them":

   * The messages whose format string is one of the `const char* const SEM_*`
     constants in **`ise/main/ise_err_msgs.h`**. Those constants are public
     API — their spelling is `"Killed %d %s thread."`, printf conversions and
     all — and rewriting them into `{}` changes a declared constant's value,
     which requirement 10 does not allow. The call stays `formatString`.
   * The three `"%I64d"` conversions in **`ise/main/ise_http.cpp`**, which
     build an HTTP `Range` header. `%I64d` is MSVC's spelling of a 64-bit
     conversion; glibc reads it as the `I` flag with width 64 and prints
     something specific. Changing it changes what goes on the wire, and
     requirement 11 says nothing on the wire moves.

   Fewer than twenty-five calls to `ise::formatString` should remain in the
   sources this environment compiles.

2. **A format string is checked when it is written, not when it is
   printed.** `std::format` does this itself for a literal: the format string
   is a `std::format_string<Args...>`, consumed by a `consteval` constructor,
   so a conversion that does not match its argument is a compile error at the
   call. Two things have to be true for that to hold across this project:

   * **`Logger::writeFmt` takes a `std::format_string<Args...>`**, not a
     `const char *`, and composes its message with `std::format`, not with
     `std::vformat`. It is declared in `ise/main/ise_classes.h` and it is the
     one place this library passes a caller's format string through to a
     formatting call, so it is the one place where the check would otherwise
     be lost. `writeFmt("%d", x)` stops compiling; `writeFmt("{}", x)`
     compiles and is checked.
   * **`ise/main/ise_concepts.h` gains a fifth concept, `Formattable`**,
     naming the property `writeFmt`'s arguments need: that the type has a
     real `std::formatter` specialisation and not the disabled primary
     template the standard leaves behind. C++23 calls this
     `std::formattable`; this project is C++20, so it is written out here,
     once, under a name the constraint can be read in.

   The four concepts already declared in `ise/main/ise_concepts.h` stay
   exactly as they are.

3. **The three value types whose ordering is a member's ordering say so with
   a defaulted `operator<=>`.** They are:

   ```
   ise/main/ise_classes.h   DateTime     ordered by its time_t
   ise/main/ise_classes.h   Timestamp    ordered by its microsecond count
   ise/main/ise_socket.h    InetAddress  ordered by ip, then port
   ```

   Each writes out six relational operators today — `==`, `!=`, `<`, `>`,
   `<=`, `>=` on `DateTime` and `Timestamp`, `==` and `!=` on `InetAddress` —
   and every one of them is the same comparison of the same members in the
   same order. After this milestone each type declares

   ```cpp
   auto operator <=> (const T& rhs) const = default;
   bool operator == (const T& rhs) const = default;
   ```

   and nothing else. Both are needed: a defaulted `<=>` gives the four
   relational operators and does *not* give `==`, which is a separate
   defaulted declaration by design.

   **No hand-written relational or equality operator is left with a body in
   these sources.** The declaration order of the members is what the defaulted
   comparison uses, and for all three types it is already the order the
   hand-written operators used — check that before you delete them, because it
   is the one way this change can alter an answer.

4. **A buffer crosses an interface as one value.** On the path a buffer
   takes between the network and the code that handles it, every interface
   that today takes a `void *` (or a `const void *`, or a `char *`) and a
   separate `int` or `size_t` length takes a `std::span` instead:
   `std::span<const std::byte>` for data being read, `std::span<std::byte>`
   for a buffer being written into.

   The boundaries, by header — this list is the whole of the requirement:

   ```
   ise/main/ise_socket.h       UdpSocket::sendBuffer, UdpSocket::recvBuffer (both),
                               BaseUdpServer's received-data callback and dataReceived,
                               BaseTcpConnection::sendBuffer, BaseTcpConnection::recvBuffer
   ise/main/ise_server_tcp.h   TcpConnection::send, postSendTask (the pure virtual,
                               the base implementation and the override)
   ise/main/ise_server_udp.h   UdpPacket::setPacketBuffer, MainUdpServer::onRecvData
   ise/main/ise_svr_mod.h      classifyUdpPacket, the UDP packet dispatch,
                               filterUdpPacket, getUdpPacketActionCode
   ise/main/ise_application.h  IseBusiness::onTcpRecvComplete, and the UDP pair above
                               where they are declared pure virtual
   ise/main/ise_http.h         the onTcpRecvComplete override
   ise/main/ise_inspector.h    the onTcpRecvComplete override
   ```

   That is more than thirty-five parameters. The library's other members
   that take a pointer and a length are not on that path and are not this
   requirement's: `Buffer`'s constructor and `assign`, the reads and writes
   of `Stream`, `MemoryStream`, `FileStream` and `Packet`,
   `MemoryStream::setPointer`, the four predefined TCP packet splitters,
   `IoBuffer::append`, `CustomHttpClient`'s response-header parsing and
   `HttpClient::receiveFile`. They are public API under requirement 8 and
   keep the signatures they have. Fewer than a hundred `void *` parameters
   should remain in the sources this environment compiles, down from a
   hundred and thirty-one: what stays is those members, the frozen Windows
   arms, the thread entry points the platform demands, and the last inch
   before a system call.

   **Where the bytes come in from C, they are converted once.** Two small
   helpers in **`ise/main/ise_global_defs.h`** do it, and they are the only
   place a pointer and a length are put back together:

   ```cpp
   inline std::span<const std::byte> asBytes(const void *data, int size);
   inline std::span<std::byte> asWritableBytes(void *data, int size);
   ```

   A `recvfrom` that returns a count, a `Packet`'s own malloc'd buffer, an
   `IoBuffer`'s readable region: each becomes a span at that one line and
   stays one until the last inch. `send()` and `recvfrom()` themselves still
   take a pointer and a length, because that is the shape of the system call.

   **`std::span` does not own anything.** Every one of these spans refers to
   memory that outlives the call it is passed to, and none of them is stored
   past the call. If you find yourself wanting to keep a span in a member,
   that is the signal that the buffer it refers to needs an owner first.

5. **A search is written as a search.** Eleven places in this library look for
   something in a container by walking an index or an iterator with a
   comparison in the body and a `break` or an early `return`. Each becomes a
   ranges algorithm:

   ```
   ise/main/ise_classes.h     CustomObjectList<T>::indexOf
   ise/main/ise_classes.cpp   PropertyList::indexOf
   ise/main/ise_classes.cpp   Strings::indexOf, indexOfName, indexOfData
   ise/main/ise_classes.cpp   Strings::equals
   ise/main/ise_http.cpp      HttpHeaderStrList::indexOfName
   ise/main/ise_server_assistor.cpp  AssistorThreadPool::interruptThreadSleep
   ise/main/ise_server_tcp.cpp linePacketSplitter, nullTerminatedPacketSplitter
   ise/main/ise_server_tcp.cpp TcpConnector::findTask
   ```

   Two shapes cover all eleven:

   * A container that has iterators is searched directly.
     `CustomObjectList<T>::indexOf` is looking for "which handle owns this
     pointer", which is `std::ranges::find` with a **projection** onto the
     handle's `get` — rather than a `find_if` with a lambda that does the
     same dereference by hand. Where in the class you put the find is yours:
     `indexOf` takes the list's mutex and three of its callers hold that mutex
     already, so the search itself most likely lives in an unlocked helper,
     and what that helper is called is your business and not this
     instruction's. The search stays a member of `CustomObjectList` --
     `indexOf` itself or a member function you add -- not a free function.
   * A container that has no iterators — `Strings` and `PropertyList` expose
     `getCount()` and `getString(i)` and nothing else — is searched over its
     indices, with `std::views::iota(0, getCount())` as the range. That is not
     a rewrite of the container; it is a way to say "every index in range"
     once instead of in three parts.

   The answer each of these returns is unchanged, **including for the empty
   container and for the not-found case**, which is where an index-based
   rewrite is easiest to get wrong by one.

   The loop in `ise/main/ise_timer.cpp` that erases as it traverses is still
   not a candidate for this, and neither is any other loop whose body does
   something other than look.

6. **The two-accessor buffer interface is gone.** `UdpPacket` hands out its
   memory through `getPacketBuffer()` and its length through `getPacketSize()`
   today, and every caller has to ask for both and keep them together. After
   this milestone neither name appears in these sources, and neither does a
   parameter declared `void *packetBuffer`. What replaces them is one
   accessor:

   ```cpp
   std::span<const std::byte> getPacket() const;
   ```

   This is requirement 4 finished rather than a separate idea: a codebase that
   added spans to its interfaces and left the pointer-and-length accessors
   beside them would have both, which is worse than having one.

## 2. What must not change

7. **What the previous milestones established.** The tree you inherit is the
   output of seven milestones, and everything they established still holds.
   None of it is new work; it is here because this milestone's edits run
   through the same code.

   * **The build and the dialect.** The project is compiled as C++20 by the
     one line in the root `CMakeLists.txt` that sets it, with `-Wall`,
     `-Wextra`, `-Woverloaded-virtual` and `-Werror`, none of `-march=native`,
     `-w`, `-fpermissive` or any `-Wno-` option, target-scoped, and it builds
     clean. No source spells `throw()`; the destructors and the `what()` of the
     exception classes that carried it say `noexcept` instead. No compile line
     names a response file
     or a configuration file: the flags a translation unit is built with are
     in the command the build ran.
   * **Ownership.** No delete-expression anywhere in the sources this
     environment compiles; every new-expression's result goes straight into an
     owning handle or into a `return` that hands ownership over; Boost owns
     nothing; run under the compiler's allocation checking, every program
     reaches exit with nothing still allocated and unreachable, and reports no
     use after free, no double release and no release of something never
     allocated. The ownership is spelled in the standard's words: the
     project's own objects are held by `std::unique_ptr` where ownership is
     exclusive and by `std::shared_ptr` where it is shared.
   * **The language.** `nullptr` and no `NULL`; `override` on every override;
     scoped enumerations; `std::function` for a stored callback and a lambda —
     not a binder — at a call site that adapts one callable into another;
     range-based walks where a loop walks a container end to end, with
     `ise_timer.cpp`'s erasing loop excepted; a move for every class that
     writes a copy; a pair taken apart by naming its halves rather than by
     `.first` and `.second`; and a lookup whose result is only meaningful
     inside the test it guards declared in that test.
   * **The concurrency.** No `volatile` declaration, no `__sync_*` or
     `__atomic_*`, and none of the `pthread_create`, `pthread_join`,
     `pthread_detach`, `pthread_exit`, `pthread_mutex_*`, `pthread_cond_*`,
     `pthread_attr_*`, `pthread_once*` or `PTHREAD_MUTEX_*` spellings;
     `std::recursive_mutex`, `std::condition_variable_any`, `std::call_once`,
     `std::thread`; a monotonic clock in milliseconds; `AtomicInteger<T>`
     storing a `std::atomic<T>` and taking no lock. Cancellation, the
     counting semaphore and the scheduling parameter have no standard word
     and are still the platform's, reached through the thread object's
     `native_handle()`: `pthread_cancel`, `pthread_setschedparam` and the
     `sem_*` family stay exactly where they are. The four `ThreadImpl`
     members one thread writes while another reads -- `terminated_`,
     `isExecuting_`, `isSleepInterrupted_` and `threadId_` -- are atomic
     objects; `run()` puts a `std::thread` into the member and `waitFor()`
     joins it; no thread function takes a `void *`; and elapsed time comes
     from a monotonic clock in `getCurTicks`, with the wall clock read only
     where the time itself is wanted (`Timestamp::now`, through
     `gettimeofday`) and no sleep built from a hand-filled
     `timespec`.
   * **The vocabulary.** `Context` is `std::any`. The file and directory
     helpers are `<filesystem>`: a path is a `std::filesystem::path`, failure
     comes back as a `std::error_code` rather than as a throw, the directory
     walk is the standard iterator, and the two questions the standard cannot
     answer are still asked of the kernel -- exactly one access check and
     exactly one `stat`, with no other POSIX directory or file-entry call and
     no POSIX `rename`. A function that may have no answer returns
     `std::optional`: the pool's task hand-out returns one instead of a
     `bool` and an out-parameter, and the file-size helper has a sibling that
     answers with one rather than with -1. The twelve text and path helpers
     take `std::string_view`, and the parsers that hand their argument
     straight to a C function still take a terminated string -- a view is
     never handed to something that needs a terminator. And no declaration in
     these sources is typed `std::variant` or `boost::variant` -- the opaque
     value this library carries holds anything, not one of a fixed set.
   * **The compile-time work.** No function takes C's `...` except the private
     `ise::detail::formatStringRaw`; the packs are folded rather than
     forwarded; `intToStr` chooses its width in an `if constexpr`; the
     `[[nodiscard]]` attributes stay on the helpers that have them; no source
     outside the frozen platform branches reads an argument back out of a
     `va_list` by hand; and no question in this project is answered by
     handing the work to a parallel execution policy.
   * **The constraints.** Every class template and every function template
     this project declares still states what it requires of its parameter as a
     constraint, `ise/main/ise_concepts.h`'s four concepts are unchanged, and
     no `static_assert` comes back anywhere in the tree.

   Three of these are within reach of this milestone's edits and are worth
   naming. `Logger::writeFmt` is constrained `VarargSafe` today because it fed
   a `...`; requirement 2 replaces that constraint with `Formattable`, because
   after the change it feeds `std::format` and the property it needs is a
   different one — that is the one constraint this milestone is allowed to
   move. `CustomObjectList<T>` stays constrained `OwnedObject` while
   requirement 5 rewrites its search. And the spans of requirement 4 must not
   become a way to hand out a pointer into something nobody owns. A span is a
   pointer and a length, and the ownership rule above applies to what it
   refers to exactly as it applies to a pointer: a span handed back to a
   caller refers to storage that outlives the call, and a span that outlives
   its buffer is a use after free with a length attached.

8. **The public API of the library**, with the changes requirements 1 to 6
   name and nothing else. Every other type, function, constant and signature
   declared in `ise/main/*.h` keeps compiling and keeps meaning what it means
   today.

   This milestone changes four things a caller can see. Each is a change a
   caller has to be told about, which is why they are enumerated here:

   * A parameter pair that was `void *` and a length becomes one
     `std::span` (requirement 4). A caller passing a buffer and its size now
     passes `asBytes(buffer, size)`, or a container that converts on its own.
     A caller that **implements** one of these interfaces — an
     `IseBusiness` subclass overriding `onTcpRecvComplete`, a server module
     overriding `classifyUdpPacket` — changes its signature to match, and the
     compiler says so, because `override` is on all of them.
   * `UdpPacket::getPacketBuffer` and `getPacketSize` are replaced by
     `getPacket` (requirement 6). This removes two declared members. It is
     the one place this milestone takes something away rather than restating
     it.
   * `Logger::writeFmt` takes a `std::format_string<Args...>` and its
     conversions are `{}` (requirement 2). A caller passing a printf format
     stops compiling, at the call, with the format string named.
   * `DateTime`, `Timestamp` and `InetAddress` declare two comparison
     operators where they declared six and two (requirement 3). Every
     expression a caller could write — `a < b`, `a != b` — still compiles and
     still means the same thing; the operators it resolves to are synthesised
     rather than written.

   The fifteen programs under `examples/` are callers, and they are part of
   the tree: each of these changes reaches them, and they have to build.

9. **Nothing acquires an owner it did not have.** A `std::span` is a pointer
   and a length in one value, and it is not an owner. Replacing a `void *`
   parameter with a span does not change who frees the buffer or when, and no
   interface in requirement 4 may start copying its buffer into a container to
   make the lifetime easier. `UdpPacket` still owns its own `malloc`'d buffer
   and still frees it in its destructor.

10. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the exit statuses, the daemonisation behaviour, the signal handling, the
    timer semantics, the string and date helpers' answers for every input
    including the malformed ones, the iteration order of every container that
    has one, and the error messages, are what they are before your change.

    This is where a `std::format` conversion can go wrong quietly.
    `formatString("%s: %d", name, n)` and `std::format("{}: {}", name, n)`
    agree for those two, and they do not agree everywhere: `%.2f` is
    `{:.2f}`, `%08X` is `{:08X}`, `%-10s` is `{:<10}`, and `%p`, `%%` and a
    `%s` handed a `std::string` each need a decision. Every log line and every
    message this project emits keeps the characters it has today -- the same
    width, the same precision, the same padding -- so a conversion whose
    field moved by one space is a failure and not a detail.

11. **The build.** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake
    --build build`, from a clean tree, configures and builds the library and
    all fifteen examples with no diagnostics. The same artifacts appear in the
    same places with the same names. The project keeps its top-level
    `CMakeLists.txt`, its target-scoped warning set, and its
    `Threads::Threads` linkage.

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

12. **The frozen platform branches.** The `#ifdef ISE_WINDOWS` arms of these
    sources are not compiled in this environment and are not yours to edit.
    They are kept, byte for byte — including `ise/main/ise_iocp.cpp` in its
    entirety, which is the Windows completion-port server and passes buffers
    around as `void *` throughout. A span added to one platform's arm and not
    the other is the correct outcome: the Linux arm is the one this
    environment compiles, the Windows arm is preserved as history, and the two
    arms are not held to each other.

13. **No diagnostic is suppressed.** No `#pragma` that disables a warning, no
    `-Wno-`, no `NOLINT`, and no `(void)x;` added to silence one. If something
    warns, fix the thing it warns about. `-Wall -Wextra` under C++20 has an
    opinion about a defaulted comparison whose members are not all comparable,
    and about a `std::format` call whose argument has no formatter; both are
    worth reading rather than silencing.

    Nothing presents a header of the project, or one the build
    generates, as a system header: no pragma, line marker, include
    option or build setting does that.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_sys_utils.cpp`** — one of the two `sprintf` calls, and the
  two functions (`floatToStr`, `formatStringV`) whose two-pass length probe
  requirement 1 asks for; **`ise/main/ise_socket.cpp`** has the other.
- **`ise/main/ise_sys_utils.h`** — `formatString` and `formatStringV`, which
  stay.
- **`ise/main/ise_err_msgs.h`** — the `SEM_*` message constants that stay
  printf-shaped, and the reason requirement 1 has an exception.
- **`ise/main/ise_classes.h`** — `DateTime`, `Timestamp`, `Logger::writeFmt`,
  and `CustomObjectList<T>::indexOf`.
- **`ise/main/ise_concepts.h`** — the four concepts this project already
  declares, and where the fifth goes.
- **`ise/main/ise_global_defs.h`** — where `asBytes` and `asWritableBytes`
  belong, beside the other project-wide definitions.
- **`ise/main/ise_socket.h`**, **`ise/main/ise_server_tcp.h`**,
  **`ise/main/ise_server_udp.h`**, **`ise/main/ise_svr_mod.h`**,
  **`ise/main/ise_application.h`**, **`ise/main/ise_http.h`** and
  **`ise/main/ise_inspector.h`** — the buffer boundaries of requirement 4 and
  the two accessors of requirement 6.
- **`ise/main/ise_iocp.cpp`** — frozen, and the one file whose `void *`
  buffers stay.
- **`ise/main/*.h`** — the public API of requirement 8.
- **`examples/`** — fifteen programs that use the library the way a caller
  does; `udp_server`, `udp_client`, `echo_server`, `http_server` and the
  server modules all implement the interfaces requirement 4 changes.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of requirement
10 is defined by the code as it stands, so read the code.

## 4. Meta requirements

- The implementation budget is 14,400 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests and documents. Do not create a prose answer or an alternate entry
  point: the deliverable is the tree, building and running.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system for
  other implementations or answers.
- Leave a reproducible handoff for the next fresh session: a tree that a
  new session can configure and build with the two commands in requirement 11,
  and notes on anything you changed in the sources and why.
