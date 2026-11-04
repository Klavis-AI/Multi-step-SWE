# Step 9 — the library stops being a pile of headers

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — and it is compiled as C++20 already, with the project's
own warning set promoted to errors.

The last two milestones finished C++20's language and library halves: the
templates say what they require of their parameters, and the four standard
facilities this codebase had been hand-rolling since 1998 — `std::format`,
`operator<=>`, `std::span`, the ranges algorithms — are the ones it uses.

What is left from C++20 is the one feature that is not a facility at all.
`ise/main/` holds twenty-six headers. Every one of them is a text file that
gets pasted into every translation unit that names it, and pasted again into
the next one, and the compiler parses all of it again every time. The
project's own umbrella header, `ise/main/ise.h`, names twenty-one of the
others; every `.cpp` in the library names the umbrella or a subset of it;
each of the fifteen examples names the umbrella. That is not an accident of
this codebase — it is what `#include` is.

C++20 modules are the replacement. An interface unit is compiled once,
producing a binary artifact the compiler reads instead of re-parsing; what it
`export`s is what a consumer sees, and what it does not export is genuinely
not there. The import graph is a graph the build system knows about, not a
side effect of preprocessor ordering.

This milestone turns the library's headers into named modules, and turns the
library's own translation units into consumers of those modules.

### What this milestone is not

Modules invite two rewrites that are not being asked for here, and both of
them would break this project:

* **It is not a source-purge.** The library's headers stay, and stay
  readable, because the fifteen example programs -- and anything else that
  consumes this library the way they do -- still include them textually and
  must keep linking against the same entities the library defines. A module whose declarations are attached to the module cannot be
  linked against by a textual includer — the names are different names. The
  headers are what both halves agree on.
* **It is not a rewrite of the callers.** Nothing under `examples/` becomes a
  module or gains an `import`. Those fifteen programs are the evidence that
  the library's public surface still works the way it always did, and they
  are compiled unchanged in that role.

Four facts about this toolchain shape what you actually have to do, and all
four of them are things you will hit within an hour if you start without
them. They are stated here because measuring them costs a day and they are
not this milestone's point:

1. **A macro cannot cross a module boundary.** `import ise.socket;` does not
   bring `ISE_LINUX`, `ISE_WINDOWS`, `ISE_ASSERT` or any other object-like or
   function-like macro with it, no matter what the interface exports.
2. **A module does not forward the textual includes its interface was built
   from.** If `ise.socket`'s interface unit includes `<sys/socket.h>` in its
   global module fragment, a consumer that writes `import ise.socket;` can
   *use* the declarations that reached it through the library's own headers,
   but `sockaddr_in` and `epoll_event` and `va_start` are not declared in the
   consumer. Reachable is not visible.
3. **A namespace-scope `const` has internal linkage.** A TU that sees
   `ise/main/ise_err_msgs.h` textually *and* imports a module built from the
   same header ends up with two distinct entities, and every use of one is
   ambiguous. This project has one hundred and sixty-seven such definitions.
4. **`export using namespace X;` is not a thing**, and a module cannot export
   a macro in place of a constant.

### The six requirements

1. **The library's headers are reachable as named modules.** Every header in
   `ise/main/` that declares part of the library — that is, all of them
   except the configuration layer named in requirement 2 — belongs to exactly
   one named module, and each named module has one interface unit.

   * The modules are named `ise` and `ise.<lowercase-noun>`: `ise.socket`,
     `ise.classes`, `ise.sysutils` and so on, one segment below the root.
     Nothing else in this tree is named `ise.*`, so the name tells a reader
     and a build system the same thing.
   * `ise` itself is the umbrella. `import ise;` makes available exactly what
     `#include "ise/main/ise.h"` makes available, and its interface unit is
     the `export import` of the others — it declares nothing of its own.
   * There are **at least eight** interface units, and the natural grouping
     of twenty-one headers is around a dozen: headers that are two halves of
     one idea (`ise_svr_mod.h` and `ise_svr_mod_msgs.h`;
     `ise_server_tcp.h`, `ise_server_udp.h` and `ise_server_assistor.h`) may
     share a module, and one header does not belong to two.
   * The interface units are `.cppm` files, beside the headers they cover, in
     `ise/main/`. Use the module's own name: `ise.socket.cppm`.

2. **The configuration layer stays textual.** Five headers are
   not modularised and are still `#include`d, by interface units and by
   translation units alike:

   ```
   ise/main/ise_options.h        the build-configuration switches
   ise/main/ise_global_defs.h    ISE_LINUX / ISE_WINDOWS and the project-wide macros
   ise/main/ise_stldefs.h        the standard-library include set and the aliases
   ise/main/ise_assert.h         ISE_ASSERT
   ise/main/ise_template.h       the container templates included by macro contract
   ```

   This is fact 1 above, not a shortcut: `ISE_WINDOWS` selects which
   declarations a file even has, and a module cannot deliver it. Each
   interface unit includes these in its **global module fragment** — the
   `module;` section before the `export module` declaration — and so does
   each translation unit, textually, at the top, as it does today.

3. **The library's declarations stay attached to the global module.** An
   interface unit does not re-declare the library: it exports the
   declarations of the header it covers, and it exports them as the same
   entities a textual includer gets rather than as new ones of its own. A
   declaration inside a named module is attached to that module by default,
   and C++20 has a way to say that a declaration in an interface unit belongs
   to the global module instead; that is the property this requirement is
   about, and finding the spelling of it is part of the work.

   The consequence that matters: `libise.a` still defines
   `ise::Logger::writeStr`, `ise::formatStringV`, `ise::TcpServer::*` and
   `ise::IseApplication::*` as globally-attached names, spelled the way they
   are spelled today, and every textual includer still links against them. If
   those names acquire a module attachment they become different names, the
   examples stop linking, and the milestone is not done.

   Because of fact 3, this requires one source change that is not about
   modules at all: a namespace-scope constant that is reached both textually
   and through an import must be a single entity, so the ones in `ise/main/`
   need external linkage rather than the internal linkage a namespace-scope
   `const` has by default. The definitions inside the frozen `#ifdef ISE_WINDOWS`
   branches of those headers are not yours to touch and are not reached by
   this environment's compiles.

   `INVALID_SOCKET` is the one entity in this set that is a macro today, in
   `ise/main/ise_socket.h`. A macro is invisible to an importer; make it a
   constant of the same type and value, and every use of it keeps compiling
   because that is what it already meant.

4. **The library's translation units import rather than include.** Every
   `.cpp` under `ise/main/` — all nineteen of them — stops naming the
   library's own headers with `#include "ise/main/..."` and names the modules
   it uses with `import` instead. After this milestone, no `.cpp` in the
   library includes a project header other than the five of requirement 2.

   Fact 2 is what makes this work rather than a two-line edit. A `.cpp` that
   drops `#include "ise/main/ise_socket.h"` loses not only that header's
   declarations but everything that header was pulling in on its behalf:
   `<sys/socket.h>`, `<sys/epoll.h>`, `<signal.h>`, `<string.h>`,
   `<stdarg.h>`, `<format>` and the rest. Each translation unit states the
   standard and platform headers it actually needs, at the top, above its
   imports. A file that compiles only because some project header included
   `<string.h>` for it is a file whose dependencies were never written down;
   write them down.

   The examples are not translation units of the library and this requirement
   does not reach them: they keep their `#include "ise/main/ise.h"`.

5. **The build declares its interface units as interface units.** The
   library target gets a `FILE_SET CXX_MODULES` — `target_sources(ise PUBLIC
   FILE_SET CXX_MODULES BASE_DIRS ... FILES ...)` — listing the `.cppm`
   files. That is what makes CMake emit a scanner pass, a dyndep step, and a
   build order derived from the import graph; without it a `.cppm` is an
   unknown extension. `PUBLIC` rather than `PRIVATE`, because the modules are
   the library's interface.

   Declaring them is what makes them interface units in fact and not only
   in name: an interface unit is compiled as one, its binary artifact is
   written where the build can find it, and every translation unit that
   imports it is handed that artifact. CMake generates those arguments; you
   do not write them. A `.cppm` that is not in the file set is an unknown
   extension compiled as an ordinary source, which is a different thing that
   happens to have the same file name.

6. **The module graph is the header graph.** The imports between interface
   units reproduce the dependency structure the headers have today: if
   `ise_server_tcp.h` needs what `ise_socket.h` declares, `ise.server`
   imports `ise.socket`. An interface unit whose header's declarations appear
   in another module's interface `export import`s it, so that a consumer of
   the second sees the first, exactly as including one header today gets you
   the other.

   The headers have twenty non-configuration members and sixty-nine
   dependency edges between them; the module graph that comes out of that is
   not a chain. A design in which every module imports only `ise.classes`,
   or in which the umbrella is the only one with imports, has thrown the
   structure away and will not satisfy this.

## 2. What must not change

1. **The public API.** Every class, every member, every free function, every
   enumerator, every typedef that `ise/main/*.h` declares today is declared
   after your change, with the same name, the same signature, the same
   default arguments and the same semantics. This milestone moves
   declarations behind a module name; it does not edit them. The one
   exception is `INVALID_SOCKET` becoming a constant instead of a macro
   (requirement 3), and the one licensed addition is `inline` on the
   namespace-scope constant definitions that requirement 3 names.

2. **The headers themselves stay.** `ise/main/ise.h` and the twenty headers
   below it are still in the tree, still include what they include, and still
   work when a program outside the library includes them. Deleting a header
   because its module exists is the failure mode this requirement exists to
   name.

3. **The examples build unchanged as callers.** The fifteen programs under
   `examples/` keep `#include "ise/main/ise.h"`, gain no `import`, become no
   module, and link against the same library. They are compiled as part of
   the tree and they must build with no diagnostics. If a change of yours
   requires an edit to an example, the change is wrong.

4. **Every observable behaviour.** The command line of every program, the
   ports and protocols, what each writes to standard output, the log file at
   `<directory of the executable>/log/<executable name>.log` line for line,
   the exit statuses, the daemonisation behaviour, the signal handling, the
   timer semantics, the string and date helpers' answers for every input
   including the malformed ones, the iteration order of every container that
   has one, and the error messages, are what they are before your change.

   Modularisation is supposed to be behaviour-preserving, and the two ways it
   is not, in this codebase, are both about initialisation. A constant that
   becomes `inline` becomes one object where there were N, and a constant
   whose initialiser you rewrote while making it `inline` is a changed
   message. And a translation unit that now imports rather than includes
   still runs its own static initialisers in the same order relative to its
   own definitions: nothing in this milestone licenses moving a definition
   from one file to another.

5. **The library is still one static library with the same name.** `libise.a`
   in the same place, the same fifteen executables in the same places with
   the same names. The module artifacts (`.pcm`) are build outputs and live
   in the build directory; none of them is installed, committed, or checked
   in as a source file.

6. **Everything the earlier milestones established.** The tree you inherit is
   the output of eight milestones, and everything they established still
   holds. None of it is new work; it is here because this milestone moves
   every declaration in the library through a new kind of translation unit,
   and a rule that was true of a header is true of the interface unit that
   replaces it. A `.cppm` is a source file in this project like any other,
   and every rule that applied to a `.cpp` applies to it.

   * **The build and the dialect.** The project is compiled as C++20 by the
     one line in the root `CMakeLists.txt` that sets it, with `-Wall`,
     `-Wextra`, `-Woverloaded-virtual` and `-Werror`, none of `-march=native`,
     `-w`, `-fpermissive` or any `-Wno-` option, target-scoped, and it builds
     clean. `cmake_minimum_required` names 3.28 or later, the source lists are
     written down rather than globbed, threading comes from
     `find_package(Threads REQUIRED)` and every link names a target. The
     hand-written `build/linux` tree is gone. `libise.a` and the fifteen
     example programs are all still produced. No source spells `throw()`;
     the destructors and the `what()` of the exception classes that carried it
     say `noexcept` instead.
     Requirement 10 is the one amendment this milestone makes to the flag
     rule: the module map the build generates is a response file, and what is
     inside it counts exactly as if it were spelled on the command line.
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
     `std::filesystem::path`, failure comes back as a `std::error_code`
     rather than as a throw, the directory walk is the standard iterator, and
     the two questions the standard cannot answer are still asked of the
     kernel — exactly one access check and exactly one `stat`, with no other
     POSIX directory or file-entry call and no POSIX `rename`. A function
     that may have no answer returns `std::optional`: the pool's task
     hand-out returns one instead of a `bool` and an out-parameter, and the
     file-size helper has a sibling that answers with one rather than with
     -1. The twelve text and path helpers take `std::string_view`, and the
     parsers that hand their argument straight to a C function still take a
     terminated string — a view is never handed to something that needs a
     terminator.
   * **The compile-time work.** No function takes C's `...` except the
     private `ise::detail::formatStringRaw`; the packs are folded rather than
     forwarded; `intToStr` chooses its width in an `if constexpr`; the
     `[[nodiscard]]` attributes stay on the helpers that have them; no source
     outside the frozen platform branches reads an argument back out of a
     `va_list` by hand; and no question in this project is answered by
     handing the work to a parallel execution policy.
   * **The constraints.** Every class template and every function template
     this project declares still states what it requires of its parameter as
     a constraint, the four concepts of `ise/main/ise_concepts.h` are
     unchanged, and no `static_assert` comes back anywhere in the
     tree. A template that moves into an interface unit keeps its constraint
     with it.
   * **The library vocabulary of the last milestone.** `Logger::writeFmt`
     takes a `std::format_string` and composes its message with
     `std::format`, not with `std::vformat` and not from a `const char *`;
     `DateTime`, `Timestamp` and `InetAddress` each default their `<=>`
     and their `==` rather than writing the comparisons out; a buffer crossing
     the socket, server, server-module and application interfaces is a
     `std::span` rather than a pointer beside a length (`Buffer`, the
     streams, `Packet`, the packet splitters, `IoBuffer::append` and the HTTP
     client keep the pointer-and-length members they have), and none of
     `TcpConnection::send`, `TcpCallbacks::onTcpRecvComplete`,
     `UdpCallbacks::classifyUdpPacket`, `UdpPacket::setPacketBuffer`,
     `UdpSocket::sendBuffer` or `MainUdpServer::onRecvData` takes a
     `void *`; `UdpPacket` hands out its memory through the one accessor
     `getPacket()`, and neither `getPacketBuffer` nor `getPacketSize`
     appears in these sources; and the eleven hand-written searches are
     ranges algorithms.

   One thing about that last bullet is worth saying twice, because this
   milestone touches every declaration in the library and it would be easy to
   lose in the move: an entity's constraints, its `[[nodiscard]]`, its
   `noexcept` and its defaulted `==` are part of its declaration, and a
   declaration that moves from a header into an interface unit moves with all
   of them.

7. **The frozen platform branches.** The `#ifdef ISE_WINDOWS` arms of these
   sources are not compiled in this environment and are not yours to edit.
   `ise/main/ise_iocp.cpp` is almost entirely one such arm: everything
   between its `#ifdef ISE_WINDOWS` and the `#endif` that closes it stays
   exactly as it is. What is *not* inside that arm
   is its include preamble, and that preamble is an ordinary library
   translation unit's preamble: requirement 4 reaches it exactly as it
   reaches the other eighteen. A header whose Windows arm declares things
   this environment never sees is still included whole by its interface unit;
   the module system does not change which arm is live, because requirement 2
   keeps `ISE_WINDOWS` a macro that reaches every file.

8. **The build.** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake
   --build build`, from a clean tree, configures and builds the library and
   all fifteen examples with no diagnostics. The project keeps its top-level
   `CMakeLists.txt`, its target-scoped warning set, and its
   `Threads::Threads` linkage. A modules build has an ordering constraint an
   ordinary one does not — an interface unit must be compiled before anything
   that imports it — and the generator derives that from the scan. Do not
   hand-write the order.

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

9. **No diagnostic is suppressed.** No `#pragma` that disables a warning, no
   `-Wno-`, no `NOLINT`, no `-fpermissive`, and no `(void)x;` added to
   silence one. If something warns, fix the thing it warns about. Two of the
   warnings you are likely to meet here — a declaration reachable but not
   visible, and an entity defined in two module units — are the compiler
   telling you something true about requirement 3.

   Nothing presents a header of the project, or one the build
   generates, as a system header: no pragma, line marker, include
   option or build setting does that.

   The sanitizers are covered the same way. Nothing in the repository turns
   one off for any code, changes whether it stops at a report, sets its
   run-time options, calls into its run-time interface, or tests whether the
   build is instrumented. No such pragma, option, marker or sanitizer setting
   is written anywhere in the repository's sources, CMake files or scripts,
   not even in a comment that only mentions one.

10. **Nothing is passed to the compiler out of band.** No `-w`, no
    `--config`, no `-march=native`, no flags injected through the environment
    or a launcher. The only response file on any compile is the module map
    the generator produces, and what is inside it counts exactly as if it
    were spelled on the command line.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise.h`** — the umbrella header, and the list of what `import
  ise;` has to be equivalent to.
- **`ise/main/ise_options.h`**, **`ise_global_defs.h`**, **`ise_stldefs.h`**,
  **`ise_assert.h`**, **`ise_template.h`** — the configuration layer of
  requirement 2, and the macros that are the reason it stays textual.
- **`ise/main/ise_err_msgs.h`** and **`ise/main/ise_svr_mod_msgs.h`** — the
  namespace-scope message constants of requirement 3, and the largest single
  group of them.
- **`ise/main/ise_socket.h`** — `INVALID_SOCKET`, and eighty-odd more
  constants.
- **`ise/main/ise_classes.h`**, **`ise_thread.h`**, **`ise_sys_utils.h`**,
  **`ise_exceptions.h`**, **`ise_concepts.h`** — the lower layers of the
  header graph, and what most of the others depend on.
- **`ise/main/ise_server_tcp.h`**, **`ise_server_udp.h`**,
  **`ise_server_assistor.h`**, **`ise_event_loop.h`**, **`ise_epoll.h`**,
  **`ise_timer.h`**, **`ise_application.h`**, **`ise_http.h`**,
  **`ise_inspector.h`**, **`ise_svr_mod.h`**, **`ise_sys_threads.h`** — the
  upper layers, and requirement 6's edges.
- **`ise/main/*.cpp`** — the nineteen translation units of requirement 4, and
  the place to read what each one actually uses.
- **`ise/main/ise_iocp.cpp`** and the `#ifdef ISE_WINDOWS` arms — frozen.
- **`CMakeLists.txt`** — where the `ise` target is defined, and where
  requirement 5's file set goes.
- **`examples/`** — fifteen programs that include the umbrella header and
  must keep doing so.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of requirement
4 in section 2 is defined by the code as it stands, so read the code.

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
  new session can configure and build with the two commands in requirement 8
  of section 2, and notes on anything you changed in the sources and why.
