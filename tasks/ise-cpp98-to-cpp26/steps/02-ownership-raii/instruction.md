# Step 2 — object lifetimes the language owns

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake -S
. -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures and
builds all of it — and its sources are C++98, compiled with the project's own
warning set promoted to errors.

ISE manages every object it allocates by hand. Something calls `new`, keeps the
result in a raw pointer, and something else — later, elsewhere, on a path that
may or may not be taken — calls `delete`. Where that was too obviously unsafe
the project reached for Boost's smart pointers, so ownership is expressed two
ways at once: raw pointers, and `boost::scoped_ptr` and its relatives.

Move the project to C++11 and make object lifetime the language's job. After
this milestone every object the project allocates is owned, from the moment it
exists, by something whose destructor releases it; no source file of the
project destroys an object by hand; and the ownership vocabulary is the
standard library's, not Boost's and not the project's own.

### The dialect

1. The project is compiled as C++11. Every translation unit the project
   compiles — everything under `ise/main/` and under `examples/` it has
   today, and anything else the build configures — is compiled by `-std=c++11`
   or `-std=gnu++11`, and the language level is a property of the project
   rather than of one target: a source added to the library tomorrow is
   compiled the same way without anyone remembering to say so.

   C++11 and no later.

2. The warning set is unchanged and still clean: every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`,
   with none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option,
   and the build completes. The options a translation unit is compiled with
   are the ones on its own command line: no `@response-file` and no `--config`
   file supplies them from elsewhere. Raising the dialect will produce new
   diagnostics; answer them in the source.

### Ownership

3. **No source file of the project destroys an object by hand.** In everything
   this environment compiles — every translation unit the build configures,
   wherever in the tree its sources sit — there is no delete-expression left:
   no `delete`, no `delete[]`, not behind a null check, and not moved into a
   helper template whose whole body is one. Destruction is
   a destructor's job, and the destructors that do it are the standard
   library's. (The platform branches this environment never compiles are
   covered by requirement 11 instead.)

4. **Every allocation is held by something with a destructor from the moment
   it exists.** A new-expression's result goes to one of exactly two places:
   into an owning handle at the point it is made — `std::unique_ptr<T> h(new
   T(...))`, or `h.reset(new T(...))` on a handle that already exists — or
   into a `return`, for a function whose job is to hand ownership to its
   caller. It is never left in a raw local, a raw member or a raw argument for
   a later `delete` to find.

   The handle in the first of those is `std::unique_ptr` or `std::shared_ptr`
   itself; a typedef or alias of one is the same type and counts. Requirement
   6 allows a project-local handle that wraps one of them, and such a wrapper
   is built from a standard handle rather than from the new-expression —
   `MyHandle h(std::unique_ptr<T>(new T(...)))`, not `MyHandle h(new T(...))`.
   The second spelling leaves the object owned by nothing between the `new`
   and the wrapper's constructor body, which is the window this requirement is
   about.

   "Raw argument" is in that list deliberately. `sink(new T)` allocates an
   object that nothing owns for as long as the call takes to assemble: if
   another argument of the same call throws, it is already leaked, and in this
   dialect nothing inside the call can close that window. Where the function
   that will own the object takes a raw pointer — the project's own containers
   do, and requirement 8 freezes their signatures — the allocation is owned
   before the call is made and ownership is handed over at the call.

   Anything that *keeps* an object holds a handle. A member, a container
   element, a local that outlives the statement that allocated it — if the
   object is still there when the next statement runs, something with a
   destructor is holding it.

   One object is deliberately never destroyed, and that is the project's
   existing behaviour rather than a licence to leak. A singleton reached
   through `Singleton<T>::instance()` lives for the life of the process:
   other objects' destructors still use it while the process shuts down, so
   giving it a destructor that runs during static destruction changes what the
   program writes on its way out, and requirement 9 keeps that. Its allocation
   is made into a handle like every other; what is exceptional is only what
   happens after — the object is handed to a pointer that outlives every
   destructor, and is still reachable when the process exits, which is what
   requirement 7 asks of it.

   A thread that has been told to delete itself is the other object that
   outlives the handle that made it, and for the same reason the singleton
   does: something else took ownership. From the moment `run()` succeeds on a
   thread whose auto-delete is set, the thread object owns itself and frees
   itself when it finishes; a handle that went on holding it would destroy it
   a second time. The handle lets go there, and the raw member the project
   keeps afterwards is an observer that the thread's own finaliser clears, not
   an owner. Requirement 7 still reads that the same way it reads everything
   else: if nothing frees the object, it is reported as a leak.

   One limit is stated rather than left implicit. A `new` inside a class
   template cannot be read this way where the template is written, because
   what it is handed to is not known until the template is instantiated. The
   project has one such allocation today. You may have up to four; past that,
   a template is being used as a way around this requirement, and
   requirements 3 and 7 still apply to every one of them.

5. **Boost no longer owns anything.** `boost::scoped_ptr`, `boost::scoped_array`,
   `boost::shared_ptr`, `boost::shared_array`, `boost::weak_ptr`,
   `boost::intrusive_ptr` and `boost::enable_shared_from_this` are gone from
   the project, and so is the reference-counting machinery behind them: after
   this milestone nothing the build produces refers to any of them, directly
   or through a typedef, a template alias or a macro. (The platform branches
   this environment never compiles are covered by requirement 11 instead, as
   in requirement 3: a Boost handle that only a Windows build ever
   instantiates is not something this build produces.)

6. **The ownership the project has is the standard library's.** Exclusive
   ownership is `std::unique_ptr`; shared ownership, where an object genuinely
   has more than one owner and the last one out releases it, is
   `std::shared_ptr`. This project has both, and neither is hypothetical: most
   of what it allocates has a single owner, and the connection objects its
   network layer hands to callbacks outlive whoever started the call and are
   held today by Boost's counted handle and its `shared_from_this`. Both must
   show in what the build produces: at least eight distinct `ise::` types
   held by `std::unique_ptr`, and at least one held by `std::shared_ptr`,
   across the library and the example programs together. A project-local
   handle type that wraps one of these for its own convenience is fine —
   requirement 4 says where the new-expression goes when you write one; a
   project-local handle type that *replaces* them is not.

7. **Every program releases what it allocates and touches nothing it no longer
   owns.** Run under the compiler's allocation checking, across every program
   and every input this project's behaviour is described by, each process
   reaches exit with nothing still allocated and unreachable, and reports no
   use of memory after it was freed, no second release of the same allocation,
   and no release of something that was never allocated.

   This is the requirement the other five are in service of. A tree can satisfy
   3 to 6 by moving a leak rather than removing it — a handle whose `release()`
   feeds a raw pointer nobody destroys is a leak with better manners — and this
   is where that shows up.

### What must not change

8. **The public API of the library.** Every type, function, constant and
   signature declared in `ise/main/*.h` keeps compiling and keeps meaning what
   it means today. A caller written against today's headers, compiled as C++11
   and linked against `libise.a`, must still compile, still link, and still get
   the same answers for the same arguments.

   That applies with full force to the declarations this milestone is about.
   A member function that takes a `T*` today still takes a `T*` afterwards; one
   that returns a `T*` still returns one; a constructor that takes two `bool`s
   still takes two `bool`s. The ownership changes **behind** those
   declarations, not in them. In particular the project's own object
   containers keep their whole contract: adding a raw pointer with or without
   taking ownership of it, inserting at an index, extracting an element and
   handing ownership back to the caller, and changing whether a list owns its
   elements *while it already holds some*. Expressing all of that with a
   standard handle is the design work of this milestone.

   "Behind those declarations" is where the work goes, and it is worth being
   exact about what that covers. A class's data members, the private typedefs
   that name their types, and the private helpers that exist only to serve
   them may change type, change shape, or stop existing: none of them is
   something a caller can name. What is frozen is what a caller or a subclass
   can reach — the public and protected declarations of `ise/main/*.h`.

   There are exactly three declarations this requirement releases you from,
   and they are named here rather than left to be discovered.

   The first is the containers' subscript. Where they today hand out a
   *reference to the stored pointer* — `ObjectType*&`, which lets a caller
   write through the subscript — they may hand out the pointer itself
   instead. A list whose elements are owned by handles has no `ObjectType*`
   object to refer to, and manufacturing one that aliases a handle's internals
   would be the opposite of what this milestone is for. Everything else about
   those functions holds: same name, same parameters, same pointer to the same
   object, same exception on the same bad index.

   That release is the containers' subscript, and it does not extend to
   everything else shaped like it. `TcpSvrCreateConnCallback` also hands out a
   `BaseTcpConnection*&`, and it is not released: the caller writes the
   function that parameter is declared on, so a change there is a change to
   what the caller must write. It keeps the parameter it has.

   The second is `TcpConnectionPtr` and `ConnContextPtr`. They name a Boost
   counted handle, and requirement 5 says that family is to be gone; the
   standard counted handle takes their place under the same two names. A
   caller that spells the typedef keeps compiling and keeps getting the same
   answers; one that spells the Boost template out in full does not, and that
   is what requirement 5 asks for.

   The third is `iseThrowDataAlgoException`. It allocates its exception with
   `new` and throws the pointer — an allocation requirement 4 does not let you
   keep and no handle can hold, since a thrown pointer is owned by whoever
   catches it. Throw the exception object itself. Its name and its parameter
   do not change; what a caller catches does.

9. **Every observable behaviour.** The command line of every program, the
   ports and protocols, what each writes to standard output, the log file at
   `<directory of the executable>/log/<executable name>.log` line for line, the
   orderly shutdown on `SIGTERM`, the refusal to start a second instance — all
   exactly as they are now.

   Object lifetime is observable here, and in more ways than are obvious:

   * **The order** in which a container destroys the elements it owns.
   * **Which operations throw, and what.** An out-of-range index that throws
     today must still throw, from the same place, with the same message.
   * **Whether an operation that fails destroys what it was given.** An insert
     that refuses a duplicate either destroys the object it refused or does
     not; whichever the project does today, it still does afterwards.

   And, as before, "as it does today" includes the places where today's
   behaviour is plainly wrong. Correcting an answer the project already gives
   is not part of this milestone, and from the outside, correcting it and
   breaking it look the same.

10. **The build.** The root `CMakeLists.txt` is the project's entry point, and
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
      options and link dependencies are properties of targets and are
      attached to targets. This is a property of the project as CMake
      configures it, not of how its text is spelled: assembling one of these
      names at configure time, or reaching one through a variable that
      expands to it, is writing it.
    * Source lists stay written down. Neither `aux_source_directory` nor
      `file(GLOB ...)` decides at configure time what the project is made of.
    * Every `target_link_libraries` call uses the keyword form — the target
      name followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any
      dependency — and no link line names `pthread`, `dl` or `rt`;
      `-lpthread`, `-ldl`, `-lrt` and `${CMAKE_DL_LIBS}`, which is CMake's
      own spelling of `dl`, appear nowhere in the project's CMake files.
      Threading comes from `find_package(Threads REQUIRED)` and the imported
      target it provides.
    * The project chooses neither a compiler nor a machine. No
      `set(CMAKE_C_COMPILER ...)` or `set(CMAKE_CXX_COMPILER ...)`, no
      `-march=native`, and no assignment to `CMAKE_C_FLAGS`,
      `CMAKE_CXX_FLAGS`, their per-configuration forms such as
      `CMAKE_CXX_FLAGS_DEBUG`, or the `_INIT` variables they are seeded from
      — the image describes its own toolchain through `CMAKE_TOOLCHAIN_FILE`,
      and a project that overwrites `CMAKE_CXX_FLAGS` discards the flags it
      needs to compile at all. Nothing is interposed between CMake and that
      compiler either: no `CMAKE_CXX_COMPILER_LAUNCHER`, no
      `CXX_COMPILER_LAUNCHER` target property, and neither
      `RULE_LAUNCH_COMPILE` nor `RULE_LAUNCH_LINK` on any scope that carries
      properties. Every source is compiled by the toolchain's own compiler,
      run directly, with the options its target carries: the command recorded
      for each translation unit starts with that compiler and not with a
      wrapper around it.
    * Compile options, including the warning set of requirement 2, are
      carried on targets through `target_compile_options` rather than pushed
      into a global flags variable.

11. **The platforms this environment is not.** `ise/main/ise_iocp.cpp` and the
    `ISE_WINDOWS` branches elsewhere are never compiled here and are not dead
    code. They stay, and they stay whole. Where a change you make to shared
    code forces a matching change in one of those branches, make it; that is
    maintaining the port. Deleting an implementation because nothing here
    compiles it is not.

    A member that is declared, allocated and destroyed entirely inside one of
    those branches is not this milestone's material. Requirements 3 to 7 are
    about the code this environment compiles, and they do not reach a pointer
    no build here ever creates. Leave such a member alone, and leave the lines
    that read it alone with it: giving it a handle's type is not a change the
    shared code forces on you, it is a change you chose, and every `.get()` a
    reader then has to grow is a difference with nothing on the shared side to
    account for it.

    "Whole" is a property of the text, not a promise about it. Reindenting one
    is harmless; replacing an implementation with a comment naming what it
    used to call is not keeping it. The statements inside
    them that construct or destroy an object are the exception:
    those branches may keep the `new` and the `delete` they have today, or
    take the change a shared-code change forces on them. Every other statement
    in them — including one that only reads a member, or returns it — stays
    as it is.

12. **Nothing in the repository asks a compiler or a static analyser to stop
    reporting something about these sources.** No comment that exempts a line
    or a region from analysis, no pragma that silences a Clang or GCC
    diagnostic or presents a project header as the system's, no option that
    switches a warning off or demotes it. That covers a header the build
    generates as well, and every way of making one the system's: a pragma,
    a line marker, an include option or a build setting.
    The compiler-specific pragmas already
    inside the Windows branches are part of what requirement 11 preserves and
    are not instances of this one.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

## 2. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/*.h`** — the public API of requirement 8, and the only
  description of it.
- **`ise/main/*.cpp`** — where the lifetimes actually are.
- **`examples/`** — fifteen programs that use the library the way a caller
  does, including the ones that hand it objects to own.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of requirement 9
is defined by the code as it stands, so read the code.

## 3. Meta requirements

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
  10, and notes on anything you changed in the sources and why.
