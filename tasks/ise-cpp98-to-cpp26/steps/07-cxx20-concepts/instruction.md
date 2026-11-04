# Step 7 — requirements the compiler can read

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — and it is compiled as C++17 today, with the project's
own warning set promoted to errors.

The last milestone wrote down, for the first time, what this library's
templates need of the types they are given. It wrote them as `static_assert`s
in the bodies, because that was the strongest thing C++17 had. There are seven
of them:

```
ise/main/ise_classes.h     SignalMasker::setSignals   a signal is an integer
ise/main/ise_classes.h     AtomicInteger<T>           T is a signed integer
ise/main/ise_classes.h     CallbackList<T>            T can be tested for emptiness
ise/main/ise_classes.h     BlockingQueue<T>           T can be copied
ise/main/ise_classes.h     Singleton<T>::instance     T is not abstract
ise/main/ise_sys_utils.h   intToStr                   T is an integer
ise/main/ise_sys_utils.h   formatString               each argument survives `...`
```

A `static_assert` in a body is checked late and reports in the wrong place. It
fires only once the body is instantiated, so a declaration that is never
called is never checked; it names the line inside the header rather than the
call that was wrong; and it cannot take a function out of an overload set,
because by the time it runs the overload has already been chosen. Write
`intToStr(3.5)` today and the compiler picks `intToStr`, instantiates it, and
tells you about a line in `ise_sys_utils.h`.

C++20 moves the same statements to where they belong: onto the declaration,
where they are part of the signature, checked before the call is a call, and
reported against the caller.

This milestone is that move, for every template this library declares —
including the ones whose requirement was never written down at all, like
`CustomObjectList<T>`, which deletes what it holds and therefore needs a type
that can be deleted, and has never said so anywhere.

There is nothing else in this milestone. It does not add a feature, move a
file, or change what any program does.

### The dialect

1. **The project is compiled as C++20.** Every translation unit the project
   compiles — everything under `ise/main/` and under `examples/` it has today,
   and anything else the build configures — is compiled by `-std=c++20` or
   `-std=gnu++20`, as a property of the project rather than of one target.

   The root `CMakeLists.txt` asks for C++17 today, on the one line that sets
   it. Raise it there, and not by adding a flag to a target or to
   `CMAKE_CXX_FLAGS`.

2. **The warning set is unchanged and still clean:** every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`,
   with none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option,
   and the build completes. The options a translation unit is compiled with
   are the ones on its own command line: no `@response-file` and no `--config`
   file supplies them from elsewhere.

3. **No source spells the empty dynamic exception specification `throw()`.**
   C++17 deprecated it; C++20 removed it from the language. Eleven
   declarations in this repository still carry it: the `what()` of
   `ise::Exception` — declared in `ise/main/ise_exceptions.h` and defined in
   `ise/main/ise_exceptions.cpp` — and nine destructors, one on `Exception`
   itself and one on each of the eight classes derived from it.

   Each becomes `noexcept`, which is what `throw()` meant and the only
   spelling C++20 has for it.

   Nothing in the build will tell you about these. Both compilers still accept
   `throw()` as a deprecated alias, and the diagnostic that would name it is
   not in `-Wall -Wextra`. It is on the list because the language removed it,
   not because the toolchain complained.

### The sources

4. **The four requirements this library needs and the standard library cannot
   name become named concepts of this project's own, in a new header
   `ise/main/ise_concepts.h`.** Each is named below with the property it
   stands for, and each must be written in terms of that property:

   * **`EmptyTestable`** — a `T` that can be tested for emptiness, that is,
     contextually converted to `bool`. This is what `CallbackList<T>` needs:
     `registerCallback` drops a callback that is empty, so the type has to be
     able to say whether it is. `std::function` can; a lambda's own closure
     type cannot.
   * **`OwnedObject`** — a `T` that is a complete type and can be deleted.
     This is what `CustomObjectList<T>` and `ObjectList<T>` need: they own
     what they hold and destroy it. Deleting a pointer to an incomplete type
     is the classic silent way to leak a destructor, and it is exactly the
     case a constraint can refuse and a `static_assert` in a member body
     cannot, because the member is never instantiated.
   * **`Instantiable`** — a `T` that is an object type and is not abstract.
     This is what `Singleton<T>` needs: it hands out the one `T` it made with
     `new T()`.
   * **`VarargSafe`** — a `T` that survives C's default argument promotions,
     which is to say an arithmetic type, an enumeration type or a pointer.
     This is what `formatString` and `Logger::writeFmt` need, and it is the
     same property the previous milestone wrote as a variable template in
     `ise::detail`. That variable template goes away; this replaces it.

   Four more of the requirements in this library *are* named by the standard —
   `std::integral`, `std::signed_integral`, `std::totally_ordered` and
   `std::copy_constructible`. Use the standard's word where it has one, or a
   project alias for it if you prefer the symmetry; the property is what
   matters, and either spelling states it. The four above are different: the
   standard has no word for them, so they have to exist under these names and
   say what they are named for.

   A constraint has to be the requirement. `requires true` on a template is
   not a statement of anything — it compiles, it reads like the work is done,
   and it admits every type the unconstrained template admitted.

5. **Every class template this project declares states what it requires of its
   parameter as a constraint.** There are six, five of which carry the
   constraint on the template itself:

   ```
   ise/main/ise_classes.h   CustomObjectList   its parameter is an owned object
   ise/main/ise_classes.h   ObjectList         its parameter is an owned object
   ise/main/ise_classes.h   CallbackList       its parameter can be tested for
                                               emptiness
   ise/main/ise_classes.h   BlockingQueue      its parameter can be copied into
                                               the queue and assigned back out
   ise/main/ise_classes.h   AtomicInteger      its parameter is a signed integer
   ```

   Read each one's body before you write its constraint: the requirement is
   whatever that body already does to its parameter, and the column above is a
   summary of it rather than a substitute for it. The terse spelling
   (`template <OwnedObject T>`) and the `requires`-clause spelling are both
   fine; what matters is that the constraint is on the declaration.

   `ise/main/ise_classes.h` forward-declares four of these near the top of the
   file, before `class Buffer`. A constrained template and its declaration
   have to agree, so those forward declarations carry the same constraint —
   the compiler will not let you forget, and the error it gives when you do
   names a redeclaration rather than a constraint, which is worth recognising
   before you spend time on it.

   The sixth is **`Singleton<T>`, whose constraint goes on `instance()` and
   not on the class template**, as a trailing `requires`-clause on that one
   member.

   The reason is the one the previous milestone gave for putting its
   `static_assert` there: `Singleton<T>` is instantiated as a base class of
   `T` — `class Logger : public Singleton<Logger>` in `ise_classes.h`, and
   `class TcpInspectInfo : public Singleton<TcpInspectInfo>` in
   `ise_server_tcp.h` — at a point where `T` is still incomplete. A constraint on the class template is checked when the
   template is instantiated, which is that point, and asking whether an
   incomplete type is abstract is undefined. A constraint on the member is
   checked when the member is called, by which time `T` is complete.

6. **Every function template this project declares states what it requires of
   its parameters as a constraint.** There are nine:

   ```
   ise/main/ise_sys_utils.h   intToStr       an integer
   ise/main/ise_sys_utils.h   formatString   every argument VarargSafe
   ise/main/ise_sys_utils.h   min            ordered
   ise/main/ise_sys_utils.h   max            ordered
   ise/main/ise_sys_utils.h   ensureRange    ordered
   ise/main/ise_sys_utils.h   compare        ordered
   ise/main/ise_sys_utils.h   swap           copy-constructible and assignable
   ise/main/ise_classes.h     Logger::writeFmt   every argument VarargSafe
   ise/main/ise_classes.h     SignalMasker::setSignals   every signal an integer
   ```

   The last four in `ise_sys_utils.h` are easy to miss: they are one-line
   templates at the very bottom of the header, after the declarations, and a
   reading that walks the declaration list stops before it reaches them.
   `swap` is on the list too — its requirement is not an ordering, it is that
   the type can be copied into a temporary and assigned back.

   For a pack, the constraint applies to each element: `formatString` and
   `writeFmt` require every argument to be `VarargSafe`, and `setSignals`
   requires every signal to be an integer.

7. **No requirement on a template parameter is left stated as a
   `static_assert` in a body.** The seven listed at the top of this document
   are the whole set, and after this milestone the tree holds no
   `static_assert` at all: each is the
   constraint that requirements 5 and 6 put on the corresponding declaration,
   and the text that was in the assertion's message belongs in a comment
   beside the concept or the constraint, where it is still worth reading.

   This is the point of the milestone rather than tidying after it. The two
   spellings are not equivalent:

   * A `static_assert` in a member's body is checked only when that member is
     instantiated. `CustomObjectList<Incomplete>` compiles today all the way
     up to the first call that instantiates a member that touches the type.
   * A `static_assert` reports at its own line. A constraint reports at the
     call, and names which requirement was not satisfied.
   * A `static_assert` cannot remove a function from an overload set. A
     constraint can, which is the difference between "this call is an error"
     and "this call means the other overload".

   A note on `enable_if`, which a move to concepts usually replaces:
   there is no `enable_if` in this repository, and no SFINAE of any other
   spelling — no `sizeof`-trick, no `void_t`, no tag-dispatch on a trait. The
   codebase never used them; what it used instead was a comment, and then, one
   milestone ago, a `static_assert`. Those are the local form of the same
   thing and they are what requirement 7 replaces.

## 2. What must not change

8. **What the previous milestones established.** The tree you inherit is the
   output of six milestones, and everything they established still holds.
   None of it is new work; it is here because this milestone's work runs
   through the same code.

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
     reaches exit with nothing still allocated and unreachable, and reports no
     use of memory after it was freed, no second release of the same
     allocation, and no release of something that was never allocated.

   The language:

   * `nullptr` is the null pointer constant and `NULL` is not written in these
     sources; every override says `override`; every named enumeration is
     scoped and its enumerators are reached through it; a stored callback is
     `std::function` and `boost::function` and `boost::bind` are gone from the
     sources and from what the build produces; a call site that adapts one
     callable into another is a lambda and **not a binder**, neither
     `boost::bind` nor `std::bind`; a loop that walks a container from one end
     to the other is written as a walk, except the one in
     `ise/main/ise_timer.cpp` that erases as it traverses, which stays a loop;
     and every class that writes a copy operation can also be moved.

   The concurrency:

   * No declaration this environment compiles is `volatile`-qualified and
     neither the `__sync_*` nor the `__atomic_*` family appears in these
     sources; mutual exclusion, waiting and one-time initialisation are
     `std::recursive_mutex`, `std::condition_variable_any` and
     `std::call_once`; threads are `std::thread`; elapsed time is read from a
     monotonic clock in milliseconds, with `Timestamp::now` the one place
     the wall clock is read, through `gettimeofday`; and cancellation, the counting semaphore
     and the scheduling parameter are still the platform's, reached through
     `native_handle()`.
   * `AtomicInteger<T>` stores a `std::atomic<T>` and takes no lock to read or
     write it. Requirement 5 constrains that class; it does not touch the
     member.
   * None of the `pthread_create`, `pthread_join`, `pthread_mutex_*`,
     `pthread_cond_*` or `PTHREAD_MUTEX_*` spellings comes back.

   The vocabulary:

   * `Context` is `std::any`, `ObjectContext` stores one, the three places
     that take a value back out use `std::any_cast`, and neither `boost::any`
     nor `boost/any.hpp` is named anywhere in these sources or present in what
     the build produces.
   * The opaque value this library carries is one that can hold anything, not
     one of a fixed set: no declaration in these sources is typed
     `std::variant` or `boost::variant`. A variant here would be a different
     design, not the same design in standard vocabulary.
   * The file and directory helpers are `<filesystem>`, with the one
     `euidaccess` call and the one `stat` call kept deliberately, and none of
     `mkdir`, `rmdir`, `opendir`, `readdir`, `closedir`, `unlink` or the C
     `rename` back in.
   * A function that may have no answer says so with `std::optional`.
   * The twelve text and path helpers take `std::string_view` and the fourteen
     that hand their argument to a C function still take a terminated string.

   The compile-time work:

   * No function in this project takes C's `...` except the single private
     `ise::detail::formatStringRaw`, which hands an already-checked pack to
     the C library; `formatString`, `Logger::writeFmt` and
     `SignalMasker::setSignals` are function templates over a parameter pack;
     `formatStringV` still takes its `va_list`; and no source outside the
     frozen platform branches reads an argument back out of a `va_list` by
     hand.
   * The packs are folded rather than forwarded: `setSignals` adds its signals
     with a fold and has no loop, and `formatString` states what it can carry
     with a fold.
   * `intToStr` is one template that chooses its width in an `if constexpr`,
     not two overloads that differ only in that choice.
   * The `std::pair` halves that requirement named are bound by name, the
     conditions that own what they test declare it in the `if`, and the rest
     of the tree still reaches a pair half by position where it always did.
   * The helpers in `ise/main/ise_sys_utils.h` whose only effect is their
     return value are `[[nodiscard]]` and the ones that act are not. This
     includes `min`, `max`, `ensureRange` and `compare`, which requirement 6
     also constrains; adding a constraint to those declarations must not lose
     the attribute that is already on them.

   Two of these are within reach of this milestone's edits and are worth
   naming. `formatString`'s fold over `detail::isVarargSafe` is the one place
   where requirement 4 deletes something the previous milestone added: the
   variable template goes, the fold over it goes with it, and what is left is
   `template <VarargSafe... Args>`, which states the same property in the
   place the compiler reads first. And `setSignals` keeps its fold over
   `sigaddset`; only the assertion above it becomes a constraint.

9. **The public API of the library.** Every type, function, constant and
   signature declared in `ise/main/*.h` keeps compiling and keeps meaning what
   it means today. A caller written against today's headers and linked against
   `libise.a` must still compile, still link, and still get the same answers
   for the same arguments.

   This milestone has no exception to that, and it is worth being explicit
   about why, because a constraint *can* break a caller. Every constraint
   requirements 5 and 6 ask for states a property that the corresponding
   `static_assert` already required, or that the code already relied on:

   * A call that satisfied the `static_assert` satisfies the constraint. A
     call that did not satisfy the `static_assert` did not compile either.
   * `OwnedObject` and the four ordering constraints are new — nothing
     asserted them before. They are satisfied by every instantiation in this
     repository and in `examples/`. If you find one that is not, it is a bug
     the constraint found, and the fix is at the call site, not in the
     constraint.
   * Adding a constraint to a declaration does not change its mangled name,
     its layout, or what it returns.

10. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the exit statuses, the daemonisation behaviour, the signal handling, the
    timer semantics, the string and date helpers' answers for every input
    including the malformed ones, the iteration order of every container that
    has one, and the error messages, are what they are before your change.

    This milestone is a statement about which programs compile. It is not a
    statement about what a compiling program does, and no answer in it moves.

11. **The build.** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake
    --build build`, from a clean tree, configures and builds the library and
    all fifteen examples with no diagnostics. The same artifacts appear in the
    same places with the same names. The project keeps its top-level
    `CMakeLists.txt`, its target-scoped warning set, and its `Threads::Threads`
    linkage.

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
    They are kept, byte for byte, including the Windows arms of
    `AtomicInteger` and `Singleton` that requirement 5 constrains on the Linux
    side. A constraint added to one platform's arm and not the other is the
    correct outcome here: the Linux arm is the one this environment compiles,
    the Windows arm is preserved as history, and the two arms are not held to
    each other.

    The same applies to the `stl_string_hash` template and its two `hash`
    specialisations in `ise/main/ise_stldefs.h`. They live behind
    `#ifdef ISE_USING_EXT_STL`, which nothing defines; they are not compiled,
    they are not constrained, and requirement 6's list does not include them.

13. **No diagnostic is suppressed.** No `#pragma` that disables a warning, no
    `-Wno-`, no `NOLINT`, and no `(void)x;` added to silence one. If something
    warns, fix the thing it warns about.

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

- **`ise/main/ise_classes.h`** — five of the seven `static_assert`s, the four
  forward-declared class templates near the top, `SignalMasker::setSignals`,
  `Logger::writeFmt`, and both platform arms of `AtomicInteger` and
  `Singleton`. Most of this milestone is in this file.
- **`ise/main/ise_sys_utils.h`** — the other two `static_assert`s,
  `detail::isVarargSafe`, and the five one-line generic templates at the
  bottom of the file.
- **`ise/main/ise_exceptions.h`** and **`ise/main/ise_exceptions.cpp`** — the
  eleven `throw()` declarations of requirement 3.
- **`CMakeLists.txt`** at the root — where the language level is set.
- **`ise/main/ise_event_loop.h`**, **`ise/main/ise_thread.h`**,
  **`ise/main/ise_server_tcp.h`** and **`ise/main/ise_application.h`** — the
  instantiations of `ObjectList`, `CallbackList` and `Singleton` that
  requirement 5's constraints have to accept.
- **`ise/main/*.h`** — the public API of requirement 9.
- **`examples/`** — fifteen programs that use the library the way a caller
  does.
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
