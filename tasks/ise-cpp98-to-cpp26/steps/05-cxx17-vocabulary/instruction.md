# Step 5 — the vocabulary the standard library now has

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — and it is compiled as C++11, with the project's own
warning set promoted to errors.

The last three milestones moved this project into C++11: what owns what, then
the language it is written in, then the concurrency. What is left is older
than any of them, and it is not a matter of syntax. Three kinds of statement
recur in this library that C++98 had no type for, so each of them is made
instead by a convention a caller has to be told about:

* **"there may or may not be a value here."** `ThreadPool::takeTask` returns
  a `bool` and fills in a reference. `getFileSize` returns `-1`.
* **"this is a value whose type I do not know."** `Context` — the opaque
  per-object state a caller attaches to a server or a connection — is
  `boost::any`, from a library this project now uses for almost nothing else.
* **"this is text I am only going to read."** A dozen helpers in
  `ise_sys_utils` take `const std::string&`, and every caller holding a
  `const char*` or a piece of a larger buffer allocates a string to call one.

And one subsystem is not a convention but a hand-written layer: nineteen file
and directory helpers built directly on `opendir`, `readdir`, `closedir`,
`mkdir`, `rmdir`, `unlink`, `rename` and `stat`, each with its own error
handling and its own recursion written out by hand.

C++17 has types for all four. Use them. After this milestone the three
statements above are made in the type system rather than in a comment, the
file and directory helpers are the standard library's filesystem with the same
signatures and the same answers, and Boost's `any` is gone from the sources
and from what the build produces.

### The dialect

1. **The project is compiled as C++17.** Every translation unit the project
   compiles — everything under `ise/main/` and under `examples/` it has today,
   and anything else the build configures — is compiled by `-std=c++17` or
   `-std=gnu++17`, and the language level is a property of the project rather
   than of one target: a source added to the library tomorrow is compiled the
   same way without anyone remembering to say so.

   C++17 and no later: nothing this project compiles is built at a higher
   language level, and no requirement below is answered by raising one.

2. **The warning set is unchanged and still clean:** every translation unit is
   compiled with `-Wall`, `-Wextra`, `-Woverloaded-virtual` and `-Werror`, with
   none of `-march=native`, `-w`, `-fpermissive` or any `-Wno-` option, and the
   build completes. The options a translation unit is compiled with are the
   ones on its own command line: no `@response-file` and no `--config` file
   supplies them from elsewhere.

   Raising the language level will produce new diagnostics — a construct C++11
   allowed that C++17 deprecates, a conversion that used to be implicit.
   Answer them in the source. A diagnostic that appears while you are doing
   this work is the work finding something.

### The sources

3. **The one genuinely dynamic value is the standard library's `any`.**
   `ise/main/ise_global_defs.h` declares

   ```cpp
   typedef boost::any Context;
   ```

   and that typedef is the whole of this project's answer to "hold something
   for me and give it back; I will not look at it." It is what
   `IseOptions`, `TcpServer` and every `ObjectContext` store on a caller's
   behalf, and nothing in the library ever knows what is inside one. That is
   the case the standard library's `std::any` is for, and after this milestone
   it is the type:

   * `Context` names `std::any`.
   * `ObjectContext` in `ise/main/ise_classes.h` stores a `std::any` and its
     three accessors — `setContext`, and the two `getContext` overloads — take
     and return one.
   * The three places that take a value back out — one in
     `ise/main/ise_server_tcp.cpp` and two in `ise/main/ise_http.cpp` — use
     `std::any_cast`.
   * `boost/any.hpp` is not included by anything this environment compiles,
     and the name `boost::any` does not appear in these sources.

   This is the one place in this library where a value's type really is
   unknown until run time. It is not an invitation to find others: a member
   whose type this code knows is not a candidate for `any`, and replacing one
   with `any` is the opposite of this milestone.

   The two casts behave the same way on the same input — both throw when the
   stored type is not the one asked for, and both throw something derived from
   `std::bad_cast` — so no handler in this project has to move. Nothing here
   catches either one today; if you add a handler, you have changed behaviour
   rather than preserved it.

4. **A function that may have no answer says so in its return type.** Two
   functions in this library answer a question that sometimes has no answer,
   and each says so in its own private dialect. Both get `std::optional`, and
   they get it differently, because one of them is private and the other is
   part of the library's public surface.

   * `ThreadPool::takeTask` in `ise/main/ise_thread.h` is `bool
     takeTask(Task& task)`: it returns whether it filled the reference it was
     handed. It is a **private** member — nothing outside `ThreadPool` can
     name it — so its signature is not part of the API requirement 10 freezes,
     and it changes outright. After this milestone it takes no arguments and
     returns `std::optional<Task>`, and its one caller reads the result
     rather than a `bool` beside it.

     What it must keep is the behaviour that is easy to lose in the rewrite:
     it blocks on the condition variable while the queue is empty and the pool
     is running; it returns no task exactly when it is woken with an empty
     queue and the pool has stopped; and when the pool is in "repeat" mode it
     puts the task it hands out back on the queue and notifies, before
     returning it.

   * `getFileSize` in `ise/main/ise_sys_utils.h` returns `-1` for a file it
     cannot measure. That convention is pinned — callers in this repository
     and outside it test for `-1`, and one of them is in this project's own
     examples — so `getFileSize` keeps its signature and keeps returning `-1`.
     Beside it, this milestone adds a function that answers the same question
     without the sentinel. Call it `tryGetFileSize`: same argument as
     `getFileSize`, returning `std::optional<INT64>`, empty for exactly the
     files `getFileSize` returns `-1` for and holding exactly the size
     `getFileSize` returns for the rest. `getFileSize` is then one line: the
     new function's answer, or `-1`.

     Adding a declaration to a public header is not changing the API, and this
     one is deliberate: a sentinel that has to be kept for compatibility is
     still worth having a replacement for.

   Do not go looking for a third. A function that returns a `bool` because the
   answer really is yes or no is not a function with a missing value, and
   rewriting `fileExists` to return an optional is not this requirement.

5. **The helpers that only read their text take a view of it.** Twelve
   functions declared in `ise/main/ise_sys_utils.h` take a string, read it,
   and never hand the argument itself to anything that needs it to be
   NUL-terminated or to outlive the call. Each of them takes
   `std::string_view` instead of `const std::string&` after this milestone:

   ```
   trimString   upperCase   lowerCase   repalceString (all three of its
   string parameters)   splitString   splitStringToInt   extractFilePath
   extractFileName   extractFileExt   changeFileExt (both of its string
   parameters)   pathWithSlash   pathWithoutSlash
   ```

   Their return types do not change: a function that returns a `string`
   returns a `string`. A view is what this project can *accept*; it is never
   what it hands back, because a view into a temporary outlives nothing.

   **Fourteen functions in the same header deliberately keep the string they
   have**, and this is not an oversight to be tidied up:

   ```
   sameText   compareText   isIntStr   isInt64Str   isFloatStr   isBoolStr
   strToInt   strToInt64   strToFloat   strToBool   fetchStr
   getQuotedStr   extractQuotedStr   getDequotedStr
   ```

   Every one of them ends up handing its argument's `c_str()` to a C function
   — `strtol`, `strtod`, `strcasecmp` through `compareText` — and a
   `string_view` has no `c_str()` and no promise of a terminator. Converting
   one of these to a view means either copying into a `string` inside the
   function, which is the allocation the view was supposed to remove, or
   reading past the end of the view, which is a bug: a view carries no
   terminator for a C function to stop at. `fetchStr` additionally *modifies*
   the string it is given. Leave all
   fourteen as they are.

   Nor is `std::string_view` the answer everywhere else in the project. This
   requirement is about these twelve functions. A member that stores text
   stores a `string`; a function that keeps its argument past the call keeps a
   `string`.

6. **The file and directory helpers are the standard filesystem library.**
   The block of nineteen functions under `//-- 文件和目录:` in
   `ise/main/ise_sys_utils.h` is a hand-written POSIX layer. Every one of them
   keeps the name, the signature and the answer it has today — except for the
   six that are also in requirement 5's list (`extractFilePath`,
   `extractFileName`, `extractFileExt`, `changeFileExt`, `pathWithSlash`,
   `pathWithoutSlash`), whose parameters become views, and the new
   sibling of requirement 4 — and the implementation of every one that talks
   to a filesystem becomes `<filesystem>`:

   * `fileExists`, `directoryExists`, `createDir`, `deleteDir`, `deleteFile`,
     `removeFile`, `renameFile`, `getFileSize` and `findFiles` no longer call
     `stat`, `mkdir`, `rmdir`, `unlink`, `rename`, `opendir`, `readdir` or
     `closedir`. None of those appears anywhere this environment compiles
     after this milestone.
   * A path is a `std::filesystem::path`. These functions keep taking
     their argument as a string, because their signatures do not change, but
     what they hand to `<filesystem>` is a `path` held in a named variable,
     not the caller's characters passed straight into `fs::exists`. The
     library would convert either way; what the named `path` buys is that the
     conversion happens once, at the boundary, next to the trim and the mode
     the last paragraph of this requirement is about, rather than silently at
     each of the four or five calls inside one function.
   * Failure is an error code, not an exception. Every one of these functions
     returns a `bool` or a sentinel today and must keep doing so, so use the
     `std::error_code` overloads: `fs::exists(p, ec)`, `fs::remove(p, ec)`,
     and so on. A `<filesystem>` call that throws where the old code returned
     `false` is a behaviour change, and it is the most likely one in this
     requirement.
   * `findFiles` walks a directory with `std::filesystem::directory_iterator`
     and fills the same `FileFindResult` with the same fields, in the order
     the old walk produced them, matching the `fnmatch` pattern the caller
     passed against each entry's name exactly as the old walk did.
   * `deleteDir`'s recursive form is `fs::remove_all`; its non-recursive form
     still refuses anything that is not a directory, which `rmdir` did for
     free and `fs::remove` does not.
   * `createDir` creates one directory, not a chain of them; `forceDirectories`
     is the one that creates a chain, and it is written in terms of the
     others.

   Two details of this block are worth reading the old code for before you
   rewrite it, because both are load-bearing and neither is obvious.
   `directoryExists` passes its argument through `pathWithSlash` first, which
   *trims* it — a caller has always been allowed to pass a padded string.
   And `createDir` asks `mkdir` for mode `(mode_t)-1`, which the umask
   reduces to the same permissions `fs::create_directory`'s 0777 is reduced
   to; the directories this project creates come out the same either way.

7. **Two calls to the kernel stay, and they stay on purpose.** Requirement 6
   is emphatic about what goes; this one is about what does not, and a residue
   nobody asked for is a residue somebody deletes.

   * **One call in the `euidaccess` family.** `findFiles` reports
     `FA_READ_ONLY`, which is not a question about a file's mode bits: it asks
     whether *this process* may write to it. `std::filesystem::perms` reports
     the bits, and the bits are not the answer — a file mode 0444 owned by
     root is writable by root. Keep the access call that answers it, and keep
     exactly the one `findFiles` needs; the other one in the tree today is
     `fileExists` doing an existence test, and that one becomes
     `fs::exists`.
   * **One `lstat`.** `findFiles` records a size for every entry it reports,
     including directories, and it records the size of the *entry* rather than
     of whatever a symlink points at — `item.fileSize` comes from the `lstat`
     buffer today. `std::filesystem::file_size` is specified to fail on a
     directory and to follow a symlink, so it is not that value, and there is
     no other `<filesystem>` spelling of "the size field the kernel has for
     this entry". So the one `lstat` that asks for it stays. One, in
     `findFiles`, and nowhere else. Requirement 6's ban is on `stat` itself,
     and that one goes: `getFileSize` becomes `fs::file_size` with an error
     code, `directoryExists` becomes `fs::is_directory`, and the second
     `stat` in `findFiles` — the one that follows a symlink to see whether it
     points at a directory — becomes the entry's own `status()`.

   Say why, in a comment, next to each. A kernel call with no reason written
   beside it reads as one that was missed rather than one that was kept.

8. **There is no `std::variant` in this milestone.** The sum type is the one
   vocabulary type of C++17 that this library has no honest use for: no
   function here returns one of several unrelated types, no member is a
   discriminated union, and the closest thing in the tree — the
   `PacketSplitter` callbacks and the `ISE_LINUX`/`ISE_WINDOWS` split — are a
   virtual interface and a preprocessor decision respectively, neither of
   which becomes clearer as a variant. Introducing one here would mean
   inventing a type to hold it. Do not. If you believe you have found a real
   one, say so in the handoff notes and leave the code as it is.

## 2. What must not change

9. **What the previous milestones established.** The tree you inherit is the
   output of four milestones, and everything they established still holds.
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
     `std::call_once`; threads are `std::thread`; elapsed time is read from
     a monotonic clock in milliseconds — `getCurTicks()` answers from
     `std::chrono::steady_clock::now()`, and a hand-rolled
     `clock_gettime(CLOCK_MONOTONIC, ...)` is the C interface that milestone
     replaced, not a way of honouring it; and cancellation, the counting
     semaphore and the scheduling parameter are still the platform's, reached
     through `native_handle()`.

   Four of those live in the three files this milestone edits most, so they
   are worth spelling out rather than leaving under "the concurrency":

   * `AtomicInteger<T>` in `ise/main/ise_classes.h` stores a `std::atomic<T>`
     and takes no lock to read or write it, and it says which operation it is
     performing: its bodies call `load`, `store`, `exchange`, `fetch_add` and
     `compare_exchange_strong` by name rather than leaning on the implicit
     conversion and assignment operators, which read as ordinary integer code
     and hide the fact that a synchronisation decision is being made. It is
     two headers away from `ObjectContext`, which requirement 3 rewrites.
   * `ise/main/ise_sys_utils.cpp` is where the clock and the sleeps live:
     `Timestamp::now` still reads the time with `gettimeofday` and nothing
     outside it calls `gettimeofday`, nothing calls
     `nanosleep`, and sleeping is `std::this_thread::sleep_for`. That file is
     where most of requirements 5 and 6 land.
   * `LinuxThreadImpl` in `ise/main/ise_thread.h` holds a `std::thread`,
     `run()` assigns to it, `waitFor()` joins it, there is no `pthread_t`
     member left and the thread entry point is not a `void*` function: the
     `threadExecProc` trampoline that `pthread_create` needed is gone, and
     nothing has put a differently-named `void*`-taking entry point in its
     place. That header is where requirement 4's `takeTask` lives.
   * None of the `pthread_create`, `pthread_join`, `pthread_mutex_*`,
     `pthread_cond_*` or `PTHREAD_MUTEX_*` spellings comes back into those
     files under any rewrite.

   This milestone can break two of these without looking like it is. A
   `std::filesystem::directory_iterator` walk **is** a walk, so writing one as
   an index loop over a container of entries reintroduces exactly the shape
   the language milestone removed. And `std::optional<Task>` holds a
   `std::function`, which means `ThreadPool`'s new return type is only movable
   if `Task` is — it is, but a class you add around it that declares a copy
   operation owes move operations it may not be able to write.

10. **The public API of the library**, with one named exception per
    requirement above and nothing else. Every type, function, constant and
    signature declared in `ise/main/*.h` keeps compiling and keeps meaning
    what it means today. A caller written against today's headers, compiled as
    C++17 and linked against `libise.a`, must still compile, still link, and
    still get the same answers for the same arguments.

    This milestone changes three things a caller can see, and each of them is
    a change a caller survives:

    * A parameter that was `const std::string&` becomes `std::string_view`
      (requirement 5). A call passing a `std::string`, a string literal or a
      `const char*` compiles exactly as it did and means exactly what it did.
      What stops compiling is code that took the address of one of these
      functions, and there is none in this repository.
    * `Context`, and the three accessors of `ObjectContext`, name `std::any`
      instead of `boost::any` (requirement 3). A caller that stores a value
      and casts it back is unaffected except for the spelling of the cast;
      a caller that named `boost::any` in its own declaration has to name
      `std::any` or `ise::Context`.
    * A new declaration appears beside `getFileSize` (requirement 4). Adding
      to the surface is not changing it.

    Everything else is frozen, and the freeze is not weakened by the three
    above. A class's data members, its private typedefs and its private
    helpers may change type, shape or existence — that is what makes
    `ThreadPool::takeTask` a free choice and `getFileSize` not one. The
    releases the previous milestones made are still in force and are not
    re-litigated here: enumerators are spelled through their enumeration, the
    callback typedefs name the standard callable wrapper, the containers'
    subscript returns the stored pointer by value, `TcpConnectionPtr` and
    `ConnContextPtr` name the standard counted handle, `ThreadImpl`'s four
    cross-thread members are atomic objects that still read as the values they
    hold, and `iseThrowDataAlgoException` throws the exception object.

11. **Every observable behaviour.** The command line of every program, the
    ports and protocols, what each writes to standard output, the log file at
    `<directory of the executable>/log/<executable name>.log` line for line,
    the orderly shutdown on `SIGTERM`, the refusal to start a second instance
    — all exactly as they are now.

    Five places where this milestone can move behaviour without looking like
    it is:

    * **What `findFiles` reports.** Every field of every entry: the name, the
      size — including the `st_size` of a directory, which is not zero and is
      not what `file_size` would tell you — the attribute bits, and the count.
      The pattern is matched with `fnmatch` against the entry name, and the
      `.` and `..` entries are excluded the way they are today. A walk that
      reports one more entry or one fewer has changed what this library
      answers. The order is the one thing here that was never fixed: `readdir`
      promises none and `directory_iterator` promises none, so do not go out
      of your way to reproduce the old sequence.
    * **What the old walk did with a symlink to a directory.** The
      hand-written recursion followed one; `fs::remove_all` does not, and
      deletes the link. That is a fix rather than a regression, but it is a
      difference, so make it deliberately and write it down.
    * **When a filesystem call throws.** `<filesystem>`'s throwing overloads
      raise `filesystem_error` for things the old code returned `false` for: a
      path that does not exist, a directory that is not empty, a permission
      denied. Every one of these helpers returns a `bool` or a sentinel, and
      an exception escaping one of them is a crash where there used to be a
      `false`.
    * **What a view points at after the call that made it.** A
      `std::string_view` parameter does not own anything, and a function that
      stores one, returns one, or hands one to something that outlives the
      call is writing a dangling reference that will usually still pass a
      test. Nothing in requirement 5's list needs to do any of those; if your
      rewrite of one of them wants to, the rewrite is wrong.
    * **What `takeTask` does at shutdown.** The pool stops by setting its flag
      and notifying every waiter. A rewrite that returns an empty optional
      one iteration early, or that blocks after the flag is set, hangs the
      process at exit rather than failing a test.

    And, as before, "as it does today" includes the places where today's
    behaviour is plainly wrong. Correcting the answer the project gives is not
    part of this milestone. If you keep a bug deliberately, say so in the
    handoff notes so the next session knows it was read and kept rather than
    missed.

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
      spelling of `dl`, appear nowhere in the project's CMake files.
      Threading comes from `find_package(Threads REQUIRED)` and the imported
      target it provides. `<filesystem>` needs nothing added here: this
      toolchain's standard library carries it, and a project that starts
      naming `stdc++fs` on a link line is working around a compiler it is not
      being built with.
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

13. **The platforms this environment is not.** `ise/main/ise_iocp.cpp` and the
    `ISE_WINDOWS` branches elsewhere are never compiled here and are not dead
    code. They stay, and they stay whole. Where a change you make to shared
    code forces a matching change in one of those branches, make it; that is
    maintaining the port. Deleting an implementation because nothing here
    compiles it is not.

    This milestone meets those branches in three places, and all three are
    smaller than they look:

    * The file and directory helpers of requirement 6 are mostly written
      twice, once per platform, with `#ifdef ISE_WINDOWS` around the Windows
      arm: `GetFileAttributesA`, `CreateDirectoryA`, `MoveFileA`,
      `FindFirstFileA`. Those arms are frozen. `<filesystem>` is portable and
      it would be defensible, in another project, to delete both arms and
      write one; here it is not, because requirement 13 is what keeps this
      port whole and a Windows maintainer is not in the room to agree.
      Rewrite the `ISE_LINUX` arm and leave the other one alone.
    * The twelve signatures of requirement 5 are declared once, outside any
      platform branch, and the Windows arms of the functions read the same
      parameters. A `std::string_view` supports `size()`, `empty()`,
      `substr()`, iteration and indexing exactly as a `std::string` does, so
      the bodies in there should compile unchanged; where one does not —
      because it calls `.c_str()` on the parameter — that is requirement 13's
      first paragraph, and the smallest change that restores it is the change
      to make.
    * `Context` is declared once and both ports use it. Nothing in a Windows
      branch names `boost::any` directly.

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
    The compiler-specific pragmas
    already inside the Windows branches are part of what requirement 13
    preserves and are not instances of this one.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_sys_utils.h`** and **`ise/main/ise_sys_utils.cpp`** — the
  twelve text helpers of requirement 5, the fourteen that keep their strings,
  and the nineteen file and directory helpers of requirements 6 and 7. This is
  the file the milestone is mostly in.
- **`ise/main/ise_global_defs.h`** — the `Context` typedef and the Boost
  includes of requirement 3.
- **`ise/main/ise_classes.h`** — `ObjectContext`, and `StrList`, whose `add`
  takes a `const char*` and is what `splitString` has to hand a terminated
  string to.
- **`ise/main/ise_thread.h`** and **`ise/main/ise_thread.cpp`** —
  `ThreadPool`, `takeTask` and the thread function that calls it.
- **`ise/main/ise_server_tcp.cpp`** and **`ise/main/ise_http.cpp`** — the
  three casts of requirement 3.
- **`ise/main/*.h`** — the public API of requirement 10.
- **`examples/`** — fifteen programs that use the library the way a caller
  does; `file_logger` and the server examples touch the filesystem helpers.
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
