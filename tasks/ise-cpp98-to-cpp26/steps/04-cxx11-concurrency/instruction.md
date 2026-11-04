# Step 4 — the concurrency the project is now written in

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake -S
. -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures and
builds all of it — and it is compiled as C++11, with the project's own warning
set promoted to errors.

The previous milestone made the sources C++11 everywhere the language could be
read off a declaration. Everywhere two threads meet, they are still C++98, and
in C++98 there was no such thing as a thread: every one of these classes is a
hand-written wrapper over the platform underneath it. A thread is a
`pthread_t`, created by a function that takes a `void*` and returns its answer
through another one. A mutex is a `pthread_mutex_t` configured by an attribute
object. A condition variable is a `pthread_cond_t` that has to be told which
mutex it belongs to at every wait. An atomic integer is a `volatile` member
and four compiler builtins. A single initialisation is a `pthread_once_t`.
Sleeping is a `timespec` computed from a `double`, and the clock the library
measures elapsed time with is the one a user can set backwards.

C++11 has words for all of it. Use them. After this milestone the concurrency
vocabulary of the sources this environment compiles is the standard library's:
the threads, the locks, the condition variables, the atomics, the once-only
initialisation and the durations all come from the standard, and the two
things the standard has no word for are still done the old way, on purpose,
reached through the door the standard provides for exactly that.

### The dialect

1. The project is compiled as C++11. Every translation unit the project
   compiles — everything under `ise/main/` and under `examples/` it has today,
   and anything else the build configures — is compiled by `-std=c++11` or
   `-std=gnu++11`, and the language level is a property of the project rather
   than of one target: a source added to the library tomorrow is compiled the
   same way without anyone remembering to say so.

   C++11 and no later: nothing this project compiles is built at a higher
   language level, and no requirement below is answered by raising one.

2. The warning set is unchanged and still clean: every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`, with
   none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option, and the
   build completes. The options a translation unit is compiled with are the
   ones on its own command line: no `@response-file` and no `--config` file
   supplies them from elsewhere.

   Requirements below will produce new diagnostics on the way — a member whose
   type changed and is now copied where it used to be assigned, an unused
   parameter left behind by a function that no longer needs it. Answer them in
   the source. A diagnostic that appears while you are doing this work is the
   work finding something.

### The sources

3. **State that two threads touch is atomic.** Nothing in what this
   environment compiles relies on `volatile` to make a read or a write safe to
   race, and nothing reaches for a compiler builtin to make one atomic. After
   this milestone no declaration in these sources is `volatile`-qualified, and
   neither the `__sync_*` nor the `__atomic_*` family appears anywhere in
   them.

   Two places have this today and both are this requirement's:

   * `AtomicInteger<T>` in `ise/main/ise_classes.h`, whose whole purpose is to
     provide atomic integer operations and which provides them with a
     `volatile T` member and four `__sync_*` builtins. Its value is stored in
     a member of its own whose type is the standard library's atomic type, and
     its seven operations — `get`,
     `set`, `getAndAdd`, `addAndGet`, `increment`, `decrement`, `getAndSet` —
     keep the names, the signatures and the answers they have now. `get`
     returns the current value; `set` and `getAndSet` return the value that
     was there before; `getAndAdd` returns the value before the addition and
     `addAndGet` the value after it.
   * The four members of `ThreadImpl` that one thread writes while another
     reads them: `terminated_`, which `terminate()` sets from outside the
     thread and the thread function polls; `isExecuting_`, which the thread
     sets and its owner reads; `isSleepInterrupted_`, which
     `interruptSleep()` sets from outside a sleeping thread; and `threadId_`,
     which the thread function fills in with its own kernel id while the
     thread that started it reads that id (`getTermElapsedSecs()` tests
     `threadId_ != 0`) and writes it (`waitFor()` clears it before it joins).
     Each of the four is atomic after this milestone. They are declared once,
     outside any platform branch, and both platforms get the same declaration.

     `threadId_` is the one of the four that no sentence about flags would
     catch, and it is not a bookkeeping detail: a thread id written by the
     thread it names and read by another thread with nothing between them is
     a data race in exactly the sense the other three are. Run this milestone's
     starting tree under ThreadSanitizer and it is reported.

   `volatile` was never the tool for this. It does not make a read or a write
   indivisible and it does not order anything around itself; what it asks for
   is that the compiler not cache the location, which is a different question
   and not the one these members needed answered.

4. **Mutual exclusion is the standard library's.** The `Mutex` class of
   `ise/main/ise_classes.h` locks and unlocks a standard library mutex. Nothing
   this environment compiles configures a mutex through an attribute object,
   initialises one by hand, or destroys one by hand — a mutex is a member with
   a type, and the type is what makes it.

   Which standard mutex is not a free choice. This class documents, and its
   callers depend on, that a thread already holding the lock may take it
   again and must then release it as many times as it took it. `std::mutex`
   does not promise that; `std::recursive_mutex` does, and that is the type.
   It is named rather than left to be deduced because more than one standard
   type satisfies the sentence — `std::recursive_timed_mutex` re-enters too —
   and this class has no use for a timed acquire. The type is a member of
   `Mutex` itself: the class holds the lock it hands out, rather than reaching
   one through some other object.

5. **Waiting for a condition is the standard library's.** The `Condition`
   class of `ise/main/ise_classes.h` waits, notifies one waiter and notifies
   all of them through a standard library condition variable. Nothing this
   environment compiles initialises, destroys, waits on, signals or broadcasts
   a `pthread_cond_t`.

   As with the mutex, which standard type is forced by what this class already
   is. A `Condition` is constructed with a reference to a `Condition::Mutex`
   and waits with that lock held — and `Condition::Mutex` is one of this
   project's own lock classes, not a `std::unique_lock<std::mutex>`. The
   standard has a condition variable that waits on anything with `lock()` and
   `unlock()`; that is the one this class can use, and `Condition::Mutex`
   already satisfies what it asks of a lock. `Condition` holds that condition
   variable as a member of its own, as `Mutex` holds its mutex.

6. **One-time initialisation is the standard library's.** `Singleton<T>` in
   `ise/main/ise_classes.h` runs its initialiser exactly once through the
   standard library's once-only facility. Nothing this environment compiles
   declares a `pthread_once_t`, initialises one from `PTHREAD_ONCE_INIT`, or
   calls `pthread_once`. The guarantee the class is built on is unchanged:
   concurrent first calls to `instance()` run `init()` once, and every caller
   sees the instance that call produced.

7. **A thread is a thread object.** Every thread this environment compiles is
   created, joined and detached through the standard library's thread type.
   Nothing in these sources calls `pthread_create`, `pthread_join`,
   `pthread_detach` or `pthread_exit`, and nothing declares or fills in a
   `pthread_attr_t`.

   The thread object is a member of the class that owns the thread:
   `LinuxThreadImpl` holds a standard thread object, and that member is what
   `run()` creates, `waitFor()` joins and the destructor releases.

   That reaches further than swapping four calls, and the places it reaches
   are the point of the requirement:

   * **The thread function is an ordinary function.** It does not take a
     `void*` and cast it back to something, and it does not return its answer
     as a `void*` to be cast back on the other side. `LinuxThreadImpl` already
     keeps the thread's return value in a member of its own; `waitFor()` can
     read it after the join instead of receiving a copy of it through the
     platform.
   * **`pthread_exit` does not end it.** The thread function returns.
     Unwinding a thread out from under the standard library's own thread
     machinery is not something the standard defines, and this function no
     longer has a reason to.
   * **"Is there a thread?" is a question the thread object answers.** The
     code currently asks it by comparing a `pthread_t` against zero — which is
     not a question POSIX defines either. The standard thread type answers it
     directly, and every place that tests the old handle asks the new object
     instead.
   * **A thread nobody will wait for is released.** Destroying a thread object
     that still owns a running thread terminates the program. Where this class
     today detaches the platform handle in its destructor, it must release the
     thread object, for the same reason and in the same place.
   * **Creation reports failure the way this class already reports it.**
     `LinuxThreadImpl::checkThreadError` logs and throws on a non-zero error
     code, and `run()` feeds it the result of the creation call. The standard
     thread type reports the same failure as an exception carrying the same
     code. The class keeps its behaviour: a thread that cannot be created
     still logs and still throws the same exception, with the same message.

8. **Time is a duration, and elapsed time is measured on a clock that only
   goes forward.** In what this environment compiles, a length of time is a
   `<chrono>` duration and a moment is a `<chrono>` time point, and the
   library's own two time primitives are built on them:

   * `sleepSeconds` sleeps through the standard library rather than through
     `nanosleep` and a hand-filled `timespec`. Its two behaviours are kept as
     the two shapes they are: with `allowInterrupt` false the whole requested
     length is slept, so the deadline is worked out once at the top and the
     sleep resumes against *that* deadline rather than restarting from a
     recomputed remainder; with `allowInterrupt` true the sleep is issued once
     and whatever it returns is accepted.

     Be exact about what the second of those does not promise. The old
     `nanosleep` loop broke out when the call came back `EINTR`, and the
     standard's sleep is not specified to return early when a signal arrives
     — the implementation this builds against does not. So `allowInterrupt`
     true means "do not sleep a second time", not "a signal cuts this short",
     and nothing in this library depends on a signal cutting a sleep short.
     The interruption the library actually relies on is
     `Thread::interruptSleep()`, which sets the flag of requirement 3 and is
     seen by `ThreadImpl::sleep`'s own polling loop at the end of the current
     slice. That loop and the length of its slice are unchanged, and
     `interruptSleep` still wakes a sleeping thread within one slice.
   * `getCurTicks` reads a monotonic clock. Its result exists to be handed to
     `getTickDiff` and read as "how many milliseconds have passed", and the
     clock it reads today is the wall clock, which an administrator or a time
     daemon can set backwards. After this milestone the clock it reads is one
     the standard guarantees never goes backwards, and its result is still
     milliseconds.

   `getCurTicks` and `getTickDiff` keep their declared signatures and their
   unit. The wall clock is still the right clock for saying what time it is,
   and the places that report a date or a timestamp go on using it:
   `Timestamp::now` keeps reading the time with `gettimeofday`, and is the only
   place in the library that still calls it.

9. **What stays, and how it is reached.** C++11 does not have a word for
   everything this project does, and pretending otherwise would cost behaviour
   this milestone is not allowed to spend. Three things stay as they are, and
   each stays for a reason that can be stated:

   * **The semaphore.** `Semaphore` in `ise/main/ise_classes.h` counts, and
     C++11 has no counting semaphore. It keeps its POSIX implementation. Do
     not replace it with a mutex and a condition variable: that is a
     different object with different behaviour,
     and `LinuxThreadImpl` uses it to hold a newly created thread at the gate
     until its owner has finished setting it up.
   * **Cancellation.** `kill()` stops a thread that will not stop itself.
     The standard has no such operation, and the comment above that function
     already says what it costs. It keeps calling `pthread_cancel`, and it
     keeps the cancellation state, cancellation type and cleanup handler the
     thread function sets up around itself.
   * **The scheduling policy and priority.** `setPolicy` and `setPriority` set
     a POSIX scheduling parameter. The standard has no such operation either.
     They keep calling `pthread_setschedparam`.

   All three are reached *through* the standard thread object rather than
   around it. The thread type provides an accessor for the underlying
   platform handle for exactly this purpose; the places above use it, and
   `LinuxThreadImpl` keeps no second copy of the handle of its own. Asking for
   that handle is only valid while the object still owns a thread, and the
   code that asks must be sure it does.

Two kinds of sentence above make two different claims, and the difference
decides what counts as done. "Nothing this environment compiles calls
`pthread_mutex_lock`" is a claim about what the compiler is given: the same
call sitting inside a Windows branch this environment never compiles is not a
call, and a name that is only mentioned is not a call either. "No source
outside the frozen platform branches still names `pthread_mutex_t`" is a claim
about the text of the sources, outside comments and outside those branches —
so a type that is declared and never used, a `PTHREAD_MUTEX_RECURSIVE` handed
to an attribute object, and a `pthread_once_t` member nothing touches all
still count even though no call remains.

The families that must not survive in that text are `pthread_mutex*`,
`pthread_mutexattr*`, `pthread_cond*`, `pthread_condattr*`, `pthread_once*`,
`pthread_attr*`, `pthread_create`, `pthread_join`, `pthread_detach`,
`pthread_exit`, and the `PTHREAD_MUTEX_*`, `PTHREAD_COND_*` and
`PTHREAD_ONCE_*` macros, together with the `__sync_*` and `__atomic_*`
builtins of requirement 3. What requirement 9 keeps is deliberately not in
that list: `pthread_cancel` and the cancellation state, type and cleanup
handler around it, `pthread_setschedparam`, and the `sem_*` family all stay,
and so does `pthread_self`. `pthread_t` stays too — `Thread::getThreadId()`
still returns one, as §2 requires — but storing one is a different thing from
naming one, and `LinuxThreadImpl` must not hold a `pthread_t` member of its
own.

## 2. What must not change

10. **What the previous milestones established.** The tree you inherit is the
    output of two milestones, and everything they established still holds.
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
      `std::shared_ptr`, and what the build produces still shows at least eight
      distinct `ise::` types held by `std::unique_ptr` and at least one held by
      `std::shared_ptr`. A member this milestone retypes must not drop one of
      those below the line.
    * Run under the compiler's allocation checking, across every program and
      every input this project's behaviour is described by, each process
      reaches exit with nothing still allocated and unreachable, and reports
      no use of memory after it was freed, no second release of the same
      allocation, and no release of something that was never allocated.

    The language:

    * Nothing becomes a pointer by being a zero: the null pointer constant is
      `nullptr` and `NULL` is not written anywhere in these sources.
    * Every member function that overrides a virtual function says `override`.
    * Every named enumeration is a scoped enumeration, and its enumerators are
      reached through it.
    * Callbacks are the standard library's: a stored callback is
      `std::function`, and `boost::function` and `boost::bind` are gone from
      the sources and from what the build produces. A call site that adapts one
      callable into another is a lambda, and **not a binder** — not
      `boost::bind` and not `std::bind`. That matters here: a thread function
      that no longer takes a `void*` is exactly the shape someone reaches for a
      binder to fix. Write the closure.
    * A loop that walks a container from one end to the other is written as a
      walk. The one loop that erases elements while it traverses them — in
      `ise/main/ise_timer.cpp` — is not a walk and was deliberately left as a
      loop; leave it as one.
    * Every class that writes a copy operation can also be moved.

    This milestone can break several of these without looking like it is. A
    lock or a thread object cannot be copied, so a class that acquires one
    stops being copyable, and a class that declares a copy operation it can no
    longer honour does not compile — nor does one whose move operations the
    compiler quietly stops providing. Where a member's new type changes what
    its class can do, say what the class can do: the rule is the one above,
    and the answer is in the declarations, not in a copy operation that
    pretends.

    Say it by not declaring, not by declaring a deletion. The rule above counts
    a class as copyable when it *declares* a copy constructor or a copy
    assignment operator of its own, and a `= delete`d one is still a
    declaration: writing `Mutex(const Mutex&) = delete;` makes `Mutex` a class
    that declares a copy operation and therefore owes a move constructor and a
    move assignment operator it cannot have. A class whose members are not
    copyable is already not copyable — leave the copy operations undeclared and
    let the members say so.

11. **The public API of the library.** Every type, function, constant and
    signature declared in `ise/main/*.h` keeps compiling and keeps meaning what
    it means today. A caller written against today's headers, compiled as
    C++11 and linked against `libise.a`, must still compile, still link, and
    still get the same answers for the same arguments.

    A class's data members, the private typedefs that name their types and the
    private helpers that exist only to serve them may change type, change
    shape, or stop existing: none of them is something a caller can name. That
    is what makes this milestone possible at all — `Mutex::mutex_`,
    `Condition::cond_`, `AtomicInteger::value_`, `Singleton::once_` and
    `LinuxThreadImpl`'s handle are every one of them private, and every one of
    them changes type here.

    What is frozen is what a caller or a subclass can reach — the public and
    protected declarations of `ise/main/*.h`. `ThreadImpl`'s four
    cross-thread members are protected, and a subclass can read them: their
    declared type changes, and what a subclass may do with them does not.
    `terminated_` still tests as a truth value, still assigns from a `bool`,
    and `isTerminated()` still returns what it returns today; `threadId_`
    still compares against `0`, still assigns from a `THREAD_ID`, and
    `getThreadId()` still returns a `THREAD_ID` rather than an atomic.

    Adding to that surface is not changing it, and this requirement releases
    you from nothing. The releases the previous milestones made are still in
    force and are not re-litigated here: enumerators are spelled through their
    enumeration, the callback typedefs name the standard callable wrapper, the
    containers' subscript returns the stored pointer by value,
    `TcpConnectionPtr` and `ConnContextPtr` name the standard counted handle,
    and `iseThrowDataAlgoException` throws the exception object.

12. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the orderly shutdown on `SIGTERM`, the refusal to start a second instance
    — all exactly as they are now.

    Four places where this milestone can move behaviour without looking like
    it is:

    * **What an atomic operation returns.** `getAndAdd` and `addAndGet` differ
      by exactly the value added, and the standard library's fetch-and-add
      returns the value from before. One of the two needs the addition
      applied to the result and the other does not; getting that backwards
      changes every sequence number, connection id and counter the project
      hands out, and changes them by one.
    * **Which thread is holding what.** The lock this project hands to
      `Condition` is recursive, and waiting on a condition variable releases
      the lock once. A thread that waits while holding the lock twice does not
      release it, and the program stops. That is true of the code as it stands
      and must be no more true afterwards: the ownership of the lock at every
      wait is what it is today.
    * **When a thread is released.** A thread object that owns a running
      thread and is then destroyed terminates the process — not an exception,
      not an error code, an immediate stop. Every path that reaches the
      destructor of the class holding one must first have joined it or
      released it, including the path where the thread deletes its own object
      from inside its own cleanup handler.
    * **What the clock says.** `getCurTicks` returns milliseconds, and
      `getTickDiff` subtracts two of its results. Changing the clock changes
      the origin, not the unit: a timeout of five hundred still fires after
      five hundred milliseconds, and a difference is still a count of
      milliseconds. A clock read in seconds, or in the standard library's own
      default unit, silently changes every timeout in the project.

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
    * Every `target_link_libraries` call uses the keyword form — the target
      name followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any
      dependency — and no link line names `pthread`, `dl` or `rt`;
      `-lpthread`, `-ldl`, `-lrt` and `${CMAKE_DL_LIBS}`, which is CMake's own
      spelling of `dl`, appear nowhere in the project's CMake files. Threading
      comes from `find_package(Threads REQUIRED)` and the imported target it
      provides — which is still where it comes from after this milestone: the
      standard library's threads are the platform's threads underneath, and
      the imported target is how this project says so.
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

14. **The platforms this environment is not.** `ise/main/ise_iocp.cpp` and the
    `ISE_WINDOWS` branches elsewhere are never compiled here and are not dead
    code. They stay, and they stay whole. Where a change you make to shared
    code forces a matching change in one of those branches, make it; that is
    maintaining the port. Deleting an implementation because nothing here
    compiles it is not.

    This milestone meets those branches more often than the last one did,
    because concurrency is where this project's two ports differ most, and
    every one of the classes above has a Windows arm sitting beside the one
    you are rewriting:

    * `Mutex` has a `CRITICAL_SECTION` and four calls on it. `Condition` has a
      handle mutex, a semaphore and a waiter count. `Semaphore` has a kernel
      semaphore. `Singleton` has a double-checked lock over a
      `volatile` pointer. `AtomicInt` and `AtomicInt64` are written twice over
      in terms of `Interlocked*` and a lock. `WinThreadImpl` is a whole thread
      implementation. Every one of them is frozen. Requirement 3's "no
      declaration is `volatile`-qualified" and requirement 7's "nothing calls
      `pthread_create`" are about the code this environment compiles;
      neither reaches inside a Windows branch, and the `volatile` members in
      there stay `volatile`.
    * The four cross-thread members of requirement 3 are the one place where
      this milestone
      changes a declaration both ports read. They are declared in `ThreadImpl`,
      outside any platform branch, so changing their type is not touching a
      frozen branch — but `WinThreadImpl` reads them, and the lines in there
      that read them stay as they are. An atomic flag reads as a truth
      value and assigns from one exactly as a `bool` does, so nothing in there
      has to change; if you find something that does, that is the one case
      requirement 14's first paragraph is about.
    * `ise_options.h` decides which port is compiled. It is not a platform
      branch to be edited around; leave it deciding what it decides.

    So the honest expectation for this milestone is that the platform branches
    stay untouched.

    "Whole" is a property of the text, not a promise about it. Reindenting one
    is harmless; replacing an implementation with a comment naming what it
    used to call is not keeping it. The statements inside them that
    construct or destroy an object are the exception, and they stay the
    exception. Every other statement in them — including one that only reads a
    member, or returns it — stays as it is.

15. **Nothing in the repository asks a compiler or a static analyser to stop
    reporting something about these sources.** No comment that exempts a line
    or a region from analysis, no pragma that silences a Clang or GCC
    diagnostic or presents a project header as the system's, no option that
    switches a warning off or demotes it. That covers a header the build
    generates as well, and every way of making one the system's: a pragma,
    a line marker, an include option or a build setting.
    The compiler-specific pragmas already
    inside the Windows branches are part of what requirement 14 preserves and
    are not instances of this one.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_classes.h`** and **`ise/main/ise_classes.cpp`** — `Mutex`,
  `Semaphore`, `Condition`, `AtomicInteger`, `AtomicInt`, `AtomicInt64` and
  `Singleton`, and the only description of any of them.
- **`ise/main/ise_thread.h`** and **`ise/main/ise_thread.cpp`** — `ThreadImpl`,
  `LinuxThreadImpl`, `Thread`, `ThreadList` and `ThreadPool`, with the thread
  function and the cleanup handler that requirements 7 and 9 are about. Read
  the comment above `LinuxThreadImpl::kill()` before you decide what
  cancellation costs.
- **`ise/main/ise_sys_utils.h`** and **`ise/main/ise_sys_utils.cpp`** —
  `sleepSeconds`, `getCurTicks` and `getTickDiff` of requirement 8.
- **`ise/main/*.h`** — the public API of requirement 11.
- **`examples/`** — fifteen programs that use the library the way a caller
  does; `thread_pool` and the server examples run threads.
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
