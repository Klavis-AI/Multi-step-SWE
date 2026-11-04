# Step 3 — the language the project is now written in

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake -S
. -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures and
builds all of it — and it is compiled as C++11, with the project's own warning
set promoted to errors.

Compiled as C++11, and written as C++98. The tree you inherit made object
lifetime the language's job; everything else about these sources still predates
the dialect they are handed to. A null pointer is a macro that expands to an
integer. A function that overrides a virtual function looks exactly like one
that does not, and you find out which by reading the base class. Enumerators
sit in the scope that surrounds their enumeration and convert to `int` on
sight. A callback is assembled by a library function that takes a member
pointer, an object and a placeholder. A loop over a container names the
iterator type, the beginning and the end, and leaves the reader to check that
it is a walk. And the value types copy where they could move.

Write the project in the language it is compiled as. After this milestone the
constructs above are the language's own: the ones the compiler can check are
checked by the compiler, the ones the standard library supplies come from the
standard library, and Boost's callable machinery is gone from the sources and
from what the build produces.

### The dialect

1. The project is compiled as C++11. Every translation unit the project
   compiles — everything under `ise/main/` and under `examples/` it has today,
   and anything else the build configures — is compiled by `-std=c++11` or
   `-std=gnu++11`, and the language level is a property of the project rather
   than of one target: a source added to the library tomorrow is compiled the
   same way without anyone remembering to say so.

   C++11 and no later. This milestone is about using that dialect, not about
   leaving it.

2. The warning set is unchanged and still clean: every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`, with
   none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option, and the
   build completes. The options a translation unit is compiled with are the
   ones on its own command line: no `@response-file` and no `--config` file
   supplies them from elsewhere.

   Several of the requirements below will produce new diagnostics on the way —
   `override` on a function that does not override one, an enumerator that no
   longer converts to `int` where something expected it to. Answer them in the
   source. A diagnostic that appears while you are doing this work is the work
   finding something.

### The sources

3. **A null pointer is written as one.** In everything this environment
   compiles, nothing becomes a pointer by being a zero: no `NULL`, no bare `0`,
   no macro or integral constant expression converted to a pointer value —
   whether it is assigned, passed as an argument, returned, compared, or used
   to initialise a member. The null pointer constant is `nullptr`, and the
   pointers that are null are spelled with it.

   This is the smallest of these requirements and the one with the widest
   reach: `NULL` reaches a pointer in a hundred and twenty-four places in the
   sources this environment compiles.
   It is also not only a spelling. `NULL` is an integer here, so a call that
   passes it to an overload set can pick the integer overload, and a comparison
   against it can compile where a comparison against a pointer would not;
   `nullptr` has a type that is a pointer and nothing else. Where changing the
   spelling changes which function is called, the one `nullptr` picks is the
   one that was meant — but requirement 11 decides that, not this requirement:
   if the project today calls the integer overload and prints something because
   of it, it still prints that afterwards.

   (The platform branches this environment never compiles are covered by
   requirement 13 instead.)

4. **A function that overrides says so.** Every member function that overrides
   a virtual function declares that it does, and says it with `override`.
   There are about five hundred such functions here and not one of them says so
   today.

   What that buys is the point of it: a function whose signature stops matching
   the one it overrides becomes a compile error instead of quietly becoming a
   second, never-called function. So the requirement is not satisfied by
   putting the word on the declarations that already match. It is satisfied
   when the word is on every one of them and the project still builds — and if
   adding it turns up a declaration that does *not* match the base, you have
   found a bug, and requirement 11 tells you what to do with it.

   `virtual` on an overriding declaration is now redundant and may go or stay;
   nothing here depends on which.

   `final` is a different matter, and the wrong default. It says the chain
   ends, and this library is designed to be derived from: its servers, its
   clients, its business object and its connection types exist to be
   subclassed, by code that is not in this repository. Sealing one of those is
   a change to what a subclass can do with today's headers, which requirement
   10 does not allow. Sealing this library's virtuals is not this
   milestone's to do, and `override` is the word for all of them: a function
   that says `final` here has not said what this requirement asks.

5. **An enumeration with a name is a type.** Every named enumeration this
   environment compiles is declared `enum class`: its enumerators are reached
   through it, and it does not convert to an integer on its own. There are
   fourteen of them, across the containers, the HTTP layer, the socket layer,
   the epoll loop and the application.

   The anonymous enumerations are not this requirement's material and are to be
   left as they are. There are eighteen of them, and they are not enumerations
   in any sense this requirement is about — they are blocks of named integer
   constants, written `enum { FM_CREATE = 0xFFFF, ... };` because C++98 had no
   other way to put a constant in a class. An anonymous enumeration has no name
   to qualify an enumerator with, and giving one a name in order to scope it
   would invent a type nothing asked for.

   The cost of this requirement is at the use sites, and paying it is the work:
   every enumerator is written through its enumeration's name, and every place
   that relied on the implicit conversion to an integer — storing an enumerator
   in an `int` member, comparing one against an integer, indexing with one,
   writing one to a stream or a log — now says what it is doing. Say it with a
   named cast, at the place that needs the integer. Requirement 11 governs what
   comes out: the value written is the value written today.

   Where an enumeration's underlying type matters — because something stores it
   in a field of a particular width, or compares it against a value that will
   not fit — fix the underlying type in the declaration rather than leaving it
   to the compiler.

6. **Callbacks are the standard library's.** The project stores callbacks in
   `boost::function`, behind thirteen public typedefs, and assembles them with
   `boost::bind` at twenty-six call sites. After this milestone:

   * A stored callback is `std::function`. The typedefs keep their names and
     their signatures and name the standard template instead.
   * A call site that adapts one callable into another is a **closure written
     where it is used** — a lambda. Not a binder: not `boost::bind`, and not
     `std::bind` either. Swapping one binder for the other is a rename, and a
     lambda is what this dialect replaced both of them with; it says in ordinary
     syntax what the placeholders and the argument-reordering rules said in a
     dialect of their own, and the compiler checks it at the point of writing.
   * A call site that only needs the function called with the arguments it
     already has does not need a closure either. Call it.

   Nothing the build produces refers to Boost's callable machinery afterwards:
   not `boost::function`, not `boost::bind`, and not the invoker and
   argument-list templates they are built out of. A typedef, a macro or a
   template alias that reaches one of them still reaches it, and the link is
   where that shows up.

   Two things to get right rather than discover:

   * **Capture by value what outlives the statement.** A closure stored in a
     `std::function` member, handed to a timer, or given to a thread runs long
     after the frame that wrote it returned. A reference capture of a local —
     or of `this`, where the closure can outlive the object — is a use after
     free, and requirement 9 still rules that out.
   * **`std::function` copies its target.** A closure that captures an owning
     handle by value cannot be stored in one, because the handle cannot be
     copied. Capture what the closure actually needs — an observer, a counted
     handle, the value itself — rather than moving the ownership into the
     callback and finding out at the template instantiation.

7. **A loop that walks a container says so.** Where a loop drives an iterator
   over a container from one end to the other, it is a range-based `for` and
   names neither the iterator type, nor the beginning, nor the end. A loop that
   counts an index and subscripts, or that calls the container's own accessor
   by position, is a different construct and is not this requirement's; the
   iterator-driven walks are.

   A loop that is not a walk stays whatever it is. In particular, a loop that
   removes elements from the container while it traverses it is not a walk: the
   iterator it advances is the one it is invalidating, and the sequencing
   between the erase and the increment is the whole content of the loop. There
   is one of those here. Writing it as a range-based `for` is not possible and
   is not asked for; leave it as a loop that says what it is doing.

8. **A value type that copies can also move.** Every class that declares a copy
   constructor or a copy assignment operator of its own also declares a move
   constructor and a move assignment operator. There are ten such classes, all
   of them in `ise/main/ise_classes.h`, and between them they are most of what
   this library hands around by value: the byte buffer, the two time types, the
   memory stream, the pointer list, the property list, the two string lists,
   the URL, and the object-list template.

   Declaring a copy operation is what took the move operations away — the
   compiler stops generating them the moment either copy operation is
   user-declared — so every one of these ten copies today where it could have
   moved. Returning one from a function, putting one into a container, handing
   one to a function that takes it by value: all of it copies the whole payload.

   What a move operation has to do is leave the source in a state that is
   valid, destructible, and assignable to. Beyond that this project defines
   nothing about a moved-from object, and neither should you: no caller here
   reads one.

   And it has to be an operation the class actually has. A move operation
   written `= delete` is a declaration that the class cannot be moved, which
   is what it could already not do, only now it says so; and a move operation
   written `= default` on a class holding a member that can be neither moved
   nor copied is defined as deleted by the compiler, which is the same
   outcome arrived at in good faith. Where a member is what stands in the way,
   that member is part of the work.

   These classes copy, and go on copying. This milestone gives them a second
   way to be transferred; it does not take the first one away, and a class that
   answers this requirement by deleting its copy operations has broken
   requirement 10 rather than met requirement 8.

## 2. What must not change

9. **The ownership the project now has.** The tree you inherit made object
   lifetime the language's job, and everything it established still holds:

   * No source file this environment compiles destroys an object by hand —
     there is no delete-expression left anywhere in it.
   * Every new-expression's result goes straight into an owning handle at the
     point it is made, or into a `return` from a function whose job is to hand
     ownership to its caller. It is never left in a raw local, a raw member or
     a raw argument.
   * Boost owns nothing: `boost::scoped_ptr`, `boost::shared_ptr` and the rest
     of that family, and the reference-counting machinery behind them, are gone
     from the sources and from what the build produces.
   * The ownership vocabulary is the standard library's — `std::unique_ptr` for
     exclusive ownership, `std::shared_ptr` where an object genuinely has more
     than one owner — and what the build produces shows it: at least eight
     distinct `ise::` types held by `std::unique_ptr` and at least one held by
     `std::shared_ptr`.
   * Run under the compiler's allocation checking, across every program and
     every input this project's behaviour is described by, each process reaches
     exit with nothing still allocated and unreachable, and reports no use of
     memory after it was freed, no second release of the same allocation, and
     no release of something that was never allocated.

   None of this is new work. It is here because this milestone's work runs
   through the same code: a closure that captures the wrong thing, a move
   operation that leaves the source owning what the destination now owns, or a
   `std::function` assignment that drops the last handle to an object still in
   use all land here first.

10. **The public API of the library.** Every type, function, constant and
    signature declared in `ise/main/*.h` keeps compiling and keeps meaning what
    it means today. A caller written against today's headers, compiled as
    C++11 and linked against `libise.a`, must still compile, still link, and
    still get the same answers for the same arguments.

    A class's data members, the private typedefs that name their types and the
    private helpers that exist only to serve them may change type, change
    shape, or stop existing: none of them is something a caller can name. What
    is frozen is what a caller or a subclass can reach — the public and
    protected declarations of `ise/main/*.h`.

    Adding to that surface is not changing it. A move constructor and a move
    assignment operator are new declarations; `override` on a declaration that
    already overrides tells a reader what was already true. Neither is a
    change a caller can be broken by.

    There are exactly two declarations this requirement releases you from, and
    they are named here rather than left to be discovered.

    The first is **how an enumerator is spelled**. Requirement 5 scopes the
    fourteen named enumerations, and a scoped enumerator is reached through
    its enumeration: `SEEK_ORIGIN::SO_BEGINNING` where the project writes
    `SO_BEGINNING`, and no implicit conversion to an integer. A caller that
    spells an enumerator the old way does not keep compiling, and that is what
    requirement 5 asks for. Everything else about those enumerations holds:
    same enumeration names, same enumerator names, same values, same
    parameters and return types declared in terms of them.

    The second is **the callback typedefs**. `Functor`, `TimerCallback`,
    `ThreadProc`, `Task`, `OutputCallback`, `NotifyEventCallback`,
    `UserSignalHandlerCallback` and the rest of the thirteen name
    `boost::function`, and requirement 6 says that template is to be gone; the
    standard one takes their place under the same names with the same
    signatures. A caller that spells the typedef keeps compiling and keeps
    getting the same answers; one that spells the Boost template out in full
    does not.

    The releases already made are still in force and are not
    re-litigated here: the containers' subscript returns the stored pointer by
    value, `TcpConnectionPtr` and `ConnContextPtr` name the standard counted
    handle, and `iseThrowDataAlgoException` throws the exception object.

11. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the orderly shutdown on `SIGTERM`, the refusal to start a second instance
    — all exactly as they are now.

    Three places where this milestone can move behaviour without looking like
    it is:

    * **What an enumerator is worth.** Scoping an enumeration does not change
      its enumerators' values, and an explicit conversion at the place that
      needs an integer produces the same integer the implicit one produced.
      A log line, a wire byte or a protocol constant that reads a particular
      number today reads that number afterwards. Changing an enumerator's
      value while you are rewriting its declaration is the easiest way to
      break this and the hardest to see.
    * **Which overload is called.** Requirement 3 changes the type of every
      null pointer constant in the project, and the type is what overload
      resolution reads. Where that changes which function runs, what the
      program does must not.
    * **The order things are destroyed in.** A move operation that leaves the
      source holding what the destination now holds destroys it twice; one
      that leaves the source holding nothing when the caller expected
      otherwise destroys it never. Both show up in requirement 9 before they
      show up here.

      Requirement 8 also changes *which* operation runs at sites nobody
      edited: a value returned from a function, or grown out of a container's
      reallocation, moves after this milestone where it copied before. A
      correct move operation makes that invisible — the destination ends up
      with what the copy would have given it, and the source is not read
      again. An incorrect one makes it visible here, at a call site that did
      not change and in a program whose source looks untouched.

    And, as before, "as it does today" includes the places where today's
    behaviour is plainly wrong. Requirement 4 will find declarations that were
    meant to override something and do not; requirement 3 will find comparisons
    that were meant to test a pointer. Correcting the answer the project gives
    is not part of this milestone. Make the declaration say what the code does
    today — and if you keep the bug deliberately, say so in the handoff notes
    so the next session knows it was read and kept rather than missed.

12. **The build.** The root `CMakeLists.txt` is the project's entry point, and
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
    * Every `target_link_libraries` call uses the keyword form — the target
      name followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any
      dependency — and no link line names `pthread`, `dl` or `rt`;
      `-lpthread`, `-ldl`, `-lrt` and `${CMAKE_DL_LIBS}`, which is CMake's own
      spelling of `dl`, appear nowhere in the project's CMake files. Threading
      comes from `find_package(Threads REQUIRED)` and the imported target it
      provides.
    * The project chooses neither a compiler nor a machine. No
      `set(CMAKE_C_COMPILER ...)` or `set(CMAKE_CXX_COMPILER ...)`, no
      `-march=native`, and no assignment to `CMAKE_C_FLAGS`,
      `CMAKE_CXX_FLAGS`, their per-configuration forms such as
      `CMAKE_CXX_FLAGS_DEBUG`, or the `_INIT` variables they are seeded from —
      the image describes its own toolchain through `CMAKE_TOOLCHAIN_FILE`,
      and a project that overwrites `CMAKE_CXX_FLAGS` discards the flags it
      needs to compile at all. Nothing is interposed between CMake and that
      compiler either: no `CMAKE_CXX_COMPILER_LAUNCHER`, no
      `CXX_COMPILER_LAUNCHER` target property, and neither
      `RULE_LAUNCH_COMPILE` nor `RULE_LAUNCH_LINK` on any scope that carries
      properties. Every source is compiled by the toolchain's own compiler,
      run directly, with the options its target carries: the command recorded
      for each translation unit starts with that compiler and not with a
      wrapper around it.
    * Compile options, including the warning set of requirement 2, are carried
      on targets through `target_compile_options` rather than pushed into a
      global flags variable.

13. **The platforms this environment is not.** `ise/main/ise_iocp.cpp` and the
    `ISE_WINDOWS` branches elsewhere are never compiled here and are not dead
    code. They stay, and they stay whole. Where a change you make to shared
    code forces a matching change in one of those branches, make it; that is
    maintaining the port. Deleting an implementation because nothing here
    compiles it is not.

    Requirements 3 to 8 are about the code this environment compiles.
    Nothing in them reaches inside those branches, and that is worth stating
    positively, because every one of the four constructs this milestone
    rewrites also appears in there, and each time it does it is out of scope:

    * The null pointers. Thirty-seven `NULL`s, spread over thirty-three lines,
      sit in text no build here compiles. Requirement 3 does not reach them.
    * The enumeration. There is one named enumeration declared inside a Windows
      branch, along with its enumerators, and it is not one of requirement 5's
      fourteen — the compiler never sees it. None of the fourteen has an
      enumerator that those branches read. One of the fourteen is read in
      there as a value: `FileStream::fileSeek` takes a `SEEK_ORIGIN`, and its
      Windows arm hands that parameter to `SetFilePointer`, which wants a
      `DWORD`. On a Windows build, scoping the enumeration would make that
      line the kind of implicit conversion requirement 5 names. No build here
      compiles it, requirement 5 does not reach it, and it is not this
      milestone's line to repair: leave it exactly as it is written. The
      conversion in the Linux arm beside it is requirement 5's, and is not
      frozen.
    * The callback typedef. One of the `boost::function` typedefs is declared
      inside a Windows branch; requirement 6 does not reach it. Two frozen
      lines do *name* a typedef that requirement 6 retypes, and they stay as
      they are: what changes is what the typedef is defined as, not what it is
      called, so a declaration that spells the name is correct before and
      after.
    * The overriding declarations. A dozen `virtual` declarations sit inside
      those branches. Requirement 4 does not reach them, and `override` does
      not go on them.

    So the honest expectation for this milestone is that the platform branches
    stay untouched. If a change you make to shared code does force a
    matching change in one of them, make it — that is still maintaining the
    port, and it is still the rule. It is just not a bill this milestone is
    expected to run up.

    "Whole" is a property of the text, not a promise about it. Reindenting one
    is harmless; replacing an implementation with a comment naming what it
    used to call is not keeping it. The statements inside them that
    construct or destroy an object are the exception, and they stay the
    exception. Every other statement in them — including one that only reads a
    member, or returns it — stays as it is.

14. **Nothing in the repository asks a compiler or a static analyser to stop
    reporting something about these sources.** No comment that exempts a line
    or a region from analysis, no pragma that silences a Clang or GCC
    diagnostic or presents a project header as the system's, no option that
    switches a warning off or demotes it. That covers a header the build
    generates as well, and every way of making one the system's: a pragma,
    a line marker, an include option or a build setting.
    The compiler-specific pragmas already
    inside the Windows branches are part of what requirement 13 preserves and
    are not instances of this one.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/*.h`** — the public API of requirement 10, the fourteen
  enumerations of requirement 5, the thirteen callback typedefs of requirement
  6 and the ten value types of requirement 8, and the only description of any
  of them.
- **`ise/main/*.cpp`** — the call sites, the loops and the null pointers.
- **`examples/`** — fifteen programs that use the library the way a caller
  does. They bind callbacks too, and requirement 6 reaches them.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of requirement
11 is defined by the code as it stands, so read the code.

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
  new session can configure and build with the two commands in requirement 12,
  and notes on anything you changed in the sources and why.
