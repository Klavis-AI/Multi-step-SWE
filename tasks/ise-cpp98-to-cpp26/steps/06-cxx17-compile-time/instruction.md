# Step 6 — the things the compiler was never asked

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — and it is compiled as C++17, with the project's own
warning set promoted to errors.

This library already has C++17's vocabulary types -- `std::any`,
`std::optional`, `std::string_view` and `<filesystem>` are in its headers and
its sources. This milestone is about a different thing the same standard
brought: places where this code
knows something the compiler is never told, and finds out at run time, or
never.

There are three of them here.

* **Three functions take C's `...`.** `formatString`, `Logger::writeFmt` and
  `SignalMasker::setSignals`. Nothing checks what is passed to any of them.
  Hand `formatString("%s", someString)` a `std::string` and it compiles, and
  the program prints whatever is at that address or stops. The third is worse
  than unchecked: its first argument is a hand-written count of how many
  arguments follow it, so `setSignals(2, SIGTERM)` reads a second signal
  number out of whatever happened to be next in the frame.
* **A pair of overloads differ in one decision.** `intToStr(int)` and
  `intToStr(INT64)` have the same body and differ in which width they print
  at — a question about a type, written twice because until C++17 the only
  way to answer a question about a type was to write the function twice.
* **Every requirement these templates place on their arguments is in a
  comment, or nowhere.** `static_assert` does not appear in this repository.
  `AtomicInteger<T>` calls `fetch_add` and subtracts one; `CallbackList<T>`
  tests its callbacks for emptiness; `BlockingQueue<T>` copies. Violate any
  of those and you get several hundred lines of instantiation backtrace
  pointing at a header you did not write.

And two smaller ones, both about saying what you mean: the places that name a
`std::pair` and then reach into it by position, and the helpers whose answer
is the only reason to call them and which say nothing when a caller drops it.

After this milestone the compiler is told all of it.

### The dialect

1. **The project is still compiled as C++17.** Every translation unit the
   project compiles — everything under `ise/main/` and under `examples/` it
   has today, and anything else the build configures — is compiled by
   `-std=c++17` or `-std=gnu++17`, as a property of the project rather than of
   one target.

   This milestone does not move the language level. Everything it asks for is
   C++17, and nothing below is answered by raising one.

2. **The warning set is unchanged and still clean:** every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`,
   with none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option,
   and the build completes. The options a translation unit is compiled with
   are the ones on its own command line: no `@response-file` and no `--config`
   file supplies them from elsewhere.

   This one bites here. `[[nodiscard]]` under `-Werror` turns every ignored
   result into a build failure, so requirement 8 is not a decoration — mark a
   function whose result the project itself throws away and the project stops
   building. That is the requirement working.

### The sources

3. **No function in this project takes C's `...`, except one.** The three
   entry points above become function templates over a parameter pack:

   ```cpp
   template <typename... Args> string formatString(const char *format, Args... args);
   template <typename... Args> void   Logger::writeFmt(const char *format, Args... args);
   template <typename... Sigs> void   SignalMasker::setSignals(Sigs... sigs);
   ```

   Three consequences, all of them the point:

   * `setSignals` **loses its count parameter.** The pack knows its own
     length; a caller writing it out again is a caller who can get it wrong.
     `setSignals(1, SIGTERM)` becomes `setSignals(SIGTERM)`, and the two call
     sites in `ise/main/ise_application.cpp` change with it. This is the one
     signature in this milestone that a caller cannot keep — see requirement
     11.
   * The `void setSignals(int, va_list)` overload goes with it. Nothing else
     calls it; it existed so the `...` form had somewhere to delegate to.
   * `formatString` and `writeFmt` keep their signatures as a caller writes
     them. `formatString("%d items", n)` is the same expression before and
     after.

   **One function may still spell `...`, and one must.** Something has to hand
   the arguments to `vsnprintf` in the end, and C's variadic convention is the
   only way across that boundary. Declare exactly one such function, call it
   `formatStringRaw`, and put it in a namespace named `detail` nested inside
   `ise` so that it is plainly not part of what a caller may use. It is what
   `formatString`'s template body calls, and it is the only `...` left in the
   project.

   `formatStringV` is not an instance of this requirement and does not change.
   `va_list` is a type; `...` is a signature that accepts anything. That
   function's parameter list is checked, it is part of the public API, and it
   stays exactly as it is — including its `va_list` parameter.

   No `va_arg` is left anywhere in what this environment compiles.

4. **What the packs are for.** A parameter pack that is only unpacked back
   into `...` is a rename. Two of the three do something with theirs, and both
   are fold expressions:

   * **`formatString` says what C's variadic convention can carry.** A `...`
     parameter accepts arithmetic types, enumerations and pointers, because
     those are what survive the default argument promotions; everything else
     is undefined behaviour at the call. Write that as a `static_assert` over
     a fold on the pack, with a message that tells a caller what to do —
     `s.c_str()` rather than `s` is the whole of it. `writeFmt` forwards to
     `formatString` and inherits the same check; it does not need its own.
   * **`setSignals` loses its loop.** The old body called `va_arg` once per
     signal inside a `for` that counted to the caller's number. The new one
     adds every element of the pack to the set in a fold, and there is no loop
     in that function afterwards. It also states what a signal is: a whole
     number, asserted the same way.

5. **The pair of integer-to-string overloads becomes one template.**
   `intToStr(int)` and `intToStr(INT64)` are one function template whose body
   chooses the width in an `if constexpr`. The branch not taken is not
   compiled, which is the reason this is one function now and was two before.

   The answers do not move. For every argument either overload accepts today,
   the template must produce the same characters it produces today — including
   the negative values, including the widths at the ends of the ranges, and
   including the types that used to reach `intToStr(int)` by conversion
   because they were narrower than an `int`. Work out which of the two old
   bodies a given type used to land in, and keep landing there.

   It also states what it is for in a `static_assert`, as `formatString` and
   `setSignals` do: a whole number, and nothing else. A call
   passing something that is not an integer is a call that used to convert
   silently.

6. **What these templates require of their arguments is written where the
   compiler reads it.** Four class templates in `ise/main/ise_classes.h` place
   a real requirement on their parameter, and each one gets a `static_assert`
   saying it, in the class, with a message a reader can act on:

   * `AtomicInteger<T>` — `std::atomic<T>::fetch_add` exists for integers, and
     `decrement()` adds `-1`.
   * `CallbackList<T>` — `registerCallback` drops a callback that is empty,
     so a `T` has to be able to say whether it is empty.
   * `BlockingQueue<T>` — `put` copies what it is given and `take` returns a
     copy.
   * `Singleton<T>` — it hands out the one `T` it made.

   `Singleton<T>`'s is the one with a trap in it. `Singleton<T>` is
   instantiated as a *base class* of `T`, at which point `T` is not yet a
   complete type, and a type trait asked about an incomplete type is
   undefined. Put that assertion inside `instance()`, which is instantiated
   when it is called and not before, and say in a comment why it is there
   rather than in the class body. For the same reason, choose a property that
   is true of the three types this project makes singletons of: a trait about
   how a constructor is reached will answer differently depending on where it
   is asked from, and this project's singletons keep their constructors
   private.

   Together with requirements 3 and 5 that is six requirements this project
   states to the compiler and stated to nobody before.

7. **A pair is taken apart by name, and a lookup is declared in the test that
   gives it meaning.** Two shapes, both of which this code is full of:

   * A `std::map` entry reached as `.first` and `.second`, or copied into two
     named locals on the two lines after the `for`. Name them in the
     declaration instead. The loops in `IseServerInspector::outputHelpPage`
     and the connection sweep in
     `TcpEventLoop::checkTimeout` are the clearest; `TimerQueue::addTimer`
     checks the `bool` half of what `insert` returns and never looks at the
     other, which is a pair being taken apart by position too.
   * `Type::iterator it = m.find(k);` on one line and `if (it != m.end())` on
     the next. The iterator means nothing outside that `if`, and after C++17
     it does not have to outlive it. `IseSvrModBusiness` has six of these in a
     row in `ise/main/ise_svr_mod.cpp`, `IseServerInspector::findCommandItem`
     has two nested, and `TimerQueue::cancelTimer` has one.

   This is a requirement about the sites named above and the ones exactly like
   them, not a licence to rewrite every loop in the project. A binding that
   makes a line shorter and its meaning less clear is not what this asks for.

8. **A helper whose only effect is its answer says so.** Mark the free
   functions in `ise/main/ise_sys_utils.h` that compute something and change
   nothing `[[nodiscard]]`: the predicates, the conversions, the text helpers,
   the path helpers, the file questions, the system questions. Calling one and
   dropping the result is always a mistake, and after this milestone the
   compiler says so.

   **And only those.** The rule is "its only effect is its return value", and
   the functions in that header that fail it are not marked:

   * The ones that act and report whether the act worked — `createDir`,
     `deleteDir`, `deleteFile`, `removeFile`, `renameFile`,
     `forceDirectories`. Ignoring the answer is often careless and is
     sometimes exactly right.
   * The ones that consume what they are given — `fetchStr` and
     `extractQuotedStr` both advance the argument they are handed and are
     called for that as much as for what they return.
   * The ones that move the random sequence on — `getRandom` and its
     neighbours.
   * `swap`, which exchanges the caller's two objects and has no answer to
     drop.

   The four small generic helpers at the bottom of that header — `min`,
   `max`, `ensureRange` and `compare` — are on the first list, not the
   second. They are templates defined inline rather than declarations, so a
   reading that walks the declaration list stops before reaching them; they
   compute an answer and change nothing, and the rule applies to them the
   same way it applies to `trimString`.

   Marking those too would turn `[[nodiscard]]` from a statement about a
   function into a decoration on the header, which is the failure mode this
   requirement is bounded against on both sides. Where you decide a borderline
   case one way or the other, say why in a comment next to it.

9. **There are no parallel algorithms in this milestone.** C++17's execution
   policies are the other headline feature of that standard, and they are not
   applicable here. Two independent reasons, and either one is enough:

   * There is no hand-parallelized loop in this project to replace. Its
     concurrency is threads that each own a socket or a work queue; there is
     no data-parallel loop over a container anywhere in it.
   * `std::execution::par` on this toolchain's standard library is accepted
     and ignored. The parallel backend needs a library this image does not
     carry, and without it a policy argument changes nothing except what the
     source claims to do.

   So do not introduce one. A `std::execution` policy added here would be a
   word satisfied rather than a thing done, and it would say something about
   this program that is not true.

## 2. What must not change

10. **What the tree you inherit already establishes.** Earlier migration work
    put the properties below into this tree, and every one of them still
    holds afterwards. None of it is new work; it is restated here because
    this milestone's edits run through the same code, and an edit that undoes
    one of them undoes work that is not yours to undo.

    Ownership:

    * No source file this environment compiles destroys an object by hand —
      there is no delete-expression left anywhere in it — and every
      new-expression's result goes straight into an owning handle at the point
      it is made, or into a `return` from a function whose job is to hand
      ownership to its caller.
    * Boost owns nothing, and the ownership vocabulary is the standard
      library's: exclusive ownership is `std::unique_ptr`, shared ownership is
      `std::shared_ptr`.
    * Run under the compiler's allocation checking, across every program and
      every input this project's behaviour is described by, each process
      reaches exit with nothing still allocated and unreachable, and reports
      no use of memory after it was freed, no second release of the same
      allocation, and no release of something that was never allocated.

    The language:

    * `nullptr` is the null pointer constant and `NULL` is not written in
      these sources; every override says `override`; every named enumeration
      is scoped and its enumerators are reached through it; a stored callback
      is `std::function` and `boost::function` and `boost::bind` are gone from
      the sources and from what the build produces; a call site that adapts
      one callable into another is a lambda and **not a binder**, neither
      `boost::bind` nor `std::bind`; a loop that walks a container from one
      end to the other is written as a walk, except the one in
      `ise/main/ise_timer.cpp` that erases as it traverses, which stays a
      loop; and every class that writes a copy operation can also be moved.

    The concurrency:

    * No declaration this environment compiles is `volatile`-qualified and
      neither the `__sync_*` nor the `__atomic_*` family appears in these
      sources; mutual exclusion, waiting and one-time initialisation are
      `std::recursive_mutex`, `std::condition_variable_any` and
      `std::call_once`; threads are `std::thread`; elapsed time is read from a
      monotonic clock in milliseconds, with `Timestamp::now` the one place
      the wall clock is read, through `gettimeofday`; and cancellation, the counting
      semaphore and the scheduling parameter are still the platform's, reached
      through `native_handle()`.
    * `AtomicInteger<T>` stores a `std::atomic<T>` and takes no lock to read
      or write it. Requirement 6 adds a line to that class; it does not touch
      the member.
    * None of the `pthread_create`, `pthread_join`, `pthread_mutex_*`,
      `pthread_cond_*` or `PTHREAD_MUTEX_*` spellings comes back.

    The vocabulary:

    * `Context` is `std::any`, `ObjectContext` stores one, the three places
      that take a value back out use `std::any_cast`, and neither
      `boost::any` nor `boost/any.hpp` is named anywhere in these sources or
      present in what the build produces.
    * The opaque value this library carries is one that can hold anything, not
      one of a fixed set: no declaration in these sources is typed
      `std::variant` or `boost::variant`. A variant here would be a different
      design, not the same design in standard vocabulary.
    * The file and directory helpers are `<filesystem>`, with the one
      `euidaccess` call and the one `stat` call kept deliberately, and none of
      `mkdir`, `rmdir`, `opendir`, `readdir`, `closedir`, `unlink` or the C
      `rename` back in.
    * A function that may have no answer says so with `std::optional`.
    * The twelve text and path helpers take `std::string_view` and the
      fourteen that hand their argument to a C function still take a
      terminated string.

    Two of these are within reach of this milestone's edits and are worth
    naming. `ise/main/ise_sys_utils.h` is where requirements 5 and 8 land and
    it is the header carrying the `string_view` signatures and the
    `std::optional` return — changing a parameter's type back while adding an
    attribute to the same line is the way that breaks. And requirement 8's
    list of helpers and requirement 5's twelve overlap: `intToStr` is on one,
    `trimString` on the other, and both lists are about the same declarations.

11. **The public API of the library**, with one named exception and nothing
    else. Every type, function, constant and signature declared in
    `ise/main/*.h` keeps compiling and keeps meaning what it means today. A
    caller written against today's headers, compiled as C++17 and linked
    against `libise.a`, must still compile, still link, and still get the same
    answers for the same arguments.

    The exception is `SignalMasker::setSignals`, whose count parameter
    requirement 3 removes. It is a member of a Linux-only class that nothing
    outside `ise/main/ise_application.cpp` calls.

    Everything else in this milestone is a change a caller does not see at the
    call site and does see at the link line, which is a distinction worth
    being explicit about:

    * `formatString`, `writeFmt` and `intToStr` stop being functions and
      become templates. Every existing call compiles and means what it did.
      What changes is that they are no longer defined in a `.cpp` — a template
      has to be visible where it is used, so their definitions move into the
      headers, and they are no longer exported from `libise.a` as ordinary
      symbols. That is a property of templates, not a thing to work around
      with an explicit instantiation list.
    * `formatString`'s new `static_assert` will reject a call that compiles
      today. If one exists anywhere in this repository, the call is the bug
      and the assertion found it; fix the call.
    * `[[nodiscard]]` changes no signature and no answer. It changes which
      programs compile.

12. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the orderly shutdown on `SIGTERM`, the refusal to start a second instance
    — all exactly as they are now.

    Four places where this milestone can move behaviour without looking like
    it is:

    * **What `intToStr` prints.** Requirement 5 is the whole of it: the
      template has to reproduce the old dispatch, not improve on it. The types
      to think about are the ones narrower than `int`, the unsigned ones, and
      the ones exactly as wide as `int` on this platform, because those are
      where "which overload did this used to pick" has an answer that is not
      obvious. Getting this wrong changes the text this library writes into
      log lines, HTTP headers and connection names.
    * **What `formatString` produces.** The buffer that grows until the output
      fits, the empty format, the lone `%%`, a format with no arguments at
      all, and a result longer than the first allocation — all the same
      characters as today. A rewrite that formats with something other than
      the C library is a rewrite that will differ on floating point.
    * **Which signals `SignalMasker` blocks.** Both call sites block exactly
      `SIGTERM` today and must block exactly `SIGTERM` afterwards. A fold that
      builds the set in a different order still builds the same set; a fold
      that starts from the wrong empty state does not.
    * **What a structured binding binds to.** `auto [a, b] : m` copies each
      entry; `auto& [a, b] : m` does not. Requirement 7's sites include ones
      that modify what they walk and ones that only read it, and turning the
      second kind into a copy is a slowdown while turning the first kind into
      a copy is a change of behaviour that a test which only reads the
      container will not see.

    And, as before, "as it does today" includes the places where today's
    behaviour is plainly wrong. Correcting the answer the project gives is not
    part of this milestone. If you keep a bug deliberately, say so in the
    handoff notes so the next session knows it was read and kept rather than
    missed.

13. **The build.** The root `CMakeLists.txt` is the project's entry point, and
    `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` followed by `cmake --build
    build` configures and builds everything with no arguments beyond those. It
    still produces `libise.a` and the fifteen example programs under exactly
    the names they have now, and the hand-written Linux build under
    `build/linux/` stays gone rather than reappearing beside it.

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
      to targets. This is a property of the project as CMake configures it,
      not of how its text is spelled: assembling one of these names at
      configure time, or reaching one through a variable that expands to it,
      is writing it.
    * Source lists stay written down. Neither `aux_source_directory` nor
      `file(GLOB ...)` decides at configure time what the project is made of.
      A `.cpp` that becomes empty because its contents moved into a header is
      still a source file the list names; if you remove one, remove it from
      the list too.
    * Every `target_link_libraries` call uses the keyword form — the target
      name followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any
      dependency — and no link line names `pthread`, `dl`, `rt` or
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
      property, and neither `RULE_LAUNCH_COMPILE` nor `RULE_LAUNCH_LINK` on
      any scope that carries properties. Every source is compiled by the
      toolchain's own compiler, run directly, with the options its target
      carries.
    * Compile options, including the warning set of requirement 2, are carried
      on targets through `target_compile_options` rather than pushed into a
      global flags variable.

14. **The platforms this environment is not.** `ise/main/ise_iocp.cpp` and the
    `ISE_WINDOWS` branches elsewhere are never compiled here and are not dead
    code. They stay, and they stay whole. Where a change you make to shared
    code forces a matching change in one of those branches, make it; that is
    maintaining the port. Deleting an implementation because nothing here
    compiles it is not.

    This milestone meets those branches in five places:

    * `intToStr(INT64)` has an `ISE_WINDOWS` arm, and it is the one place in
      this milestone where a statement inside a platform branch need not stay
      as it is. The arm exists for one reason: the function formats through
      `sprintf`, `sprintf` needs a length modifier, and `%I64d` is the
      modifier that compiler's C library understands where `%lld` is not. The
      modifier is the whole content of the branch -- it answers "how wide is
      this value" in a format string. Requirement 5 makes the width a property
      of `T`, answered in the branch the compiler keeps, so a template that
      converts without a format string has no modifier to choose and no arm to
      carry. The rule that follows is narrow and it is the only one of its
      kind here: if your `intToStr` still formats through a `printf`-family
      call, the Windows side still spells `%I64d`; if it does not format
      through one, there is nothing owed. Every other statement in every other
      platform branch, in this file and in all the others, stays exactly
      as it is.
    * `formatStringV` has an `ISE_COMPILER_VC` arm built on `_vscprintf`. It
      is untouched by this milestone; `formatStringRaw` delegates to
      `formatStringV` and inherits both arms.
    * `SignalMasker` is inside `#ifdef ISE_LINUX` in its entirety. There is no
      Windows arm of it to keep in step.
    * `Singleton<T>` is defined twice in `ise/main/ise_classes.h`, once in
      each platform's arm, so there are two `instance()` functions.
      Requirement 6's assertion goes into the one in the `ISE_LINUX` arm,
      which is the one this environment compiles; the `ISE_WINDOWS` one stays
      exactly as it is.
    * `ise/main/ise_iocp.cpp` has eight `iter->second` sites in it, and
      requirement 7 does not reach them: nothing here compiles that file, a
      change to it cannot be checked, and an unchecked edit to a frozen
      platform arm is exactly what requirement 14 exists to prevent. Leave it
      alone.

    "Whole" is a property of the text, not a promise about it. Reindenting one
    is harmless; replacing an implementation with a comment naming what it
    used to call is not keeping it. Every statement in them stays as it is.

15. **Nothing in the repository asks a compiler or a static analyser to stop
    reporting something about these sources.** No comment that exempts a line
    or a region from analysis, no pragma that silences a Clang or GCC
    diagnostic or presents a project header as the system's, no option that
    switches a warning off or demotes it. That covers a header the build
    generates as well, and every way of making one the system's: a pragma,
    a line marker, an include option or a build setting.
    The compiler-specific pragmas
    already inside the Windows branches are part of what requirement 14
    preserves and are not instances of this one.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

    This requirement and requirement 8 meet each other. `[[nodiscard]]` under
    `-Werror` can fail the build at a call site you did not expect, and the
    two ways out of that are to fix the call site or to decide the function
    should not have been marked. Silencing the diagnostic is not a third way.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_sys_utils.h`** and **`ise/main/ise_sys_utils.cpp`** —
  `formatString`, `formatStringV`, the two `intToStr` overloads, and the
  helpers requirement 8 marks and the ones it does not. This is the file the
  milestone is mostly in.
- **`ise/main/ise_classes.h`** — `SignalMasker`, `Logger::writeFmt`, and the
  four class templates of requirement 6.
- **`ise/main/ise_classes.cpp`** — the bodies of `setSignals` and `writeFmt`
  as they are today.
- **`ise/main/ise_application.cpp`** — the two `setSignals` call sites.
- **`ise/main/ise_svr_mod.cpp`**, **`ise/main/ise_inspector.cpp`**,
  **`ise/main/ise_timer.cpp`** and **`ise/main/ise_server_tcp.cpp`** — the
  sites of requirement 7.
- **`ise/main/*.h`** — the public API of requirement 11.
- **`examples/`** — fifteen programs that use the library the way a caller
  does; several of them call `writeFmt` in a callback.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of requirement
12 is defined by the code as it stands, so read the code.

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
  new session can configure and build with the two commands in requirement 13,
  and notes on anything you changed in the sources and why.
