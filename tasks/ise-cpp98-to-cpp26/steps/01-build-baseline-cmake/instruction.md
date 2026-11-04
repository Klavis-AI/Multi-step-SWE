# Step 1 — a modern CMake build, and a tree that compiles clean under it

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. Its Linux build is a
hand-written CMake project under `build/linux/`, driven by a shell wrapper,
written against a CMake that no longer exists. This environment ships CMake
4.1 and Clang 22; the project as it stands cannot be configured by either.

Give the project a build that this decade's CMake accepts, and make the
existing sources compile under it with the project's warning set promoted to
errors. The language dialect does not change in this milestone: the code is
C++98 before and C++98 after. What changes is the build system, and whatever
the compiler has to say about the code once it is finally allowed to speak.

### The build system

1. A `CMakeLists.txt` at the repository root is the project's entry point.
   From a clean tree,

   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ```

   configures and builds everything, with no arguments beyond those.

2. The hand-written Linux build is gone, not left beside the new one:
   `build/linux/CMakeLists.txt` and `build/linux/build.sh` are removed, and
   no hand-written `Makefile` takes their place there.

3. Requirements 4 to 10 hold for **every** CMake file in the project — the
   root `CMakeLists.txt`, `examples/CMakeLists.txt`, each example's own
   `CMakeLists.txt`, and any module you add and `include()`, whatever it is
   named and whether you wrote it or generated it. CMake command names are
   case-insensitive; so is every prohibition below. They are also properties
   of the project as CMake configures it, not of how its text is spelled:
   assembling one of these commands at configure time, or reaching a banned
   name through a variable that expands to it, is writing it.

4. `cmake_minimum_required` names `VERSION 3.28` or later, and no CMake file
   anywhere in the project declares a minimum below that.

5. None of the directory-scoped and retired commands appear anywhere:
   `include_directories`, `link_directories`, `add_definitions`,
   `add_compile_options`, `add_compile_definitions`, `add_link_options`,
   `link_libraries`, `exec_program`, `build_name`, `remove`,
   `subdir_depends`, `output_required_files`, `use_mangled_mesa`,
   `utility_source`, `variable_requires`, `install_files`,
   `install_programs`, `install_targets`. Include paths, definitions,
   options and link dependencies are properties of targets and are attached
   to targets.

6. Source lists are written down. Neither `aux_source_directory` nor
   `file(GLOB ...)` decides at configure time what the project is made of.

7. Every `target_link_libraries` call uses the keyword form: the target name
   is followed by `PRIVATE`, `PUBLIC` or `INTERFACE` before any dependency.
   No link line names `pthread`, `dl` or `rt`, and `-lpthread`, `-ldl`,
   `-lrt` and `${CMAKE_DL_LIBS}` — which is CMake's own spelling of `dl` —
   appear nowhere in the project's CMake files.

8. Threading comes from `find_package(Threads REQUIRED)` and the imported
   target it provides.

9. The project chooses neither a compiler nor a machine. It does not
   `set(CMAKE_C_COMPILER ...)` or `set(CMAKE_CXX_COMPILER ...)`, it does not
   use `-march=native`, and it neither assigns nor edits `CMAKE_C_FLAGS`,
   `CMAKE_CXX_FLAGS`, the per-configuration forms of either such as
   `CMAKE_CXX_FLAGS_DEBUG`, or the `_INIT` variables they are seeded from.
   The environment already describes its toolchain through
   `CMAKE_TOOLCHAIN_FILE` — read `/opt/ise/toolchain-clang22.cmake` — and the
   flags it initialises there are needed to compile at all. A project that
   overwrites `CMAKE_CXX_FLAGS` discards them and will not build. Nor does it
   interpose anything between CMake and that compiler: no
   `CMAKE_CXX_COMPILER_LAUNCHER`, no `CXX_COMPILER_LAUNCHER` target property,
   and neither `RULE_LAUNCH_COMPILE` nor `RULE_LAUNCH_LINK` on any scope that
   carries properties. Every source is compiled by the toolchain's own
   compiler, run directly, with the options its target carries.

10. Compile options are carried on targets, through
    `target_compile_options`, rather than pushed into a global flags
    variable.

### How the code is compiled

11. Every translation unit in the project — the nineteen under `ise/main/`
    and the twenty-two under `examples/` — is compiled as C++98, by
    `-std=c++98` or `-std=gnu++98`, with `-Wall`, `-Wextra`,
    `-Woverloaded-virtual` and `-Werror`, and with none of `-march=native`,
    `-w`, `-fpermissive` or any `-Wno-` option. The options a translation
    unit is compiled with are the ones on its own command line: no
    `@response-file` and no `--config` file supplies them from elsewhere.

12. Under that warning set the build completes. Every diagnostic it reports
    is answered by correcting the source, and every correction preserves the
    behaviour described below. The project has never been compiled this
    strictly; expect real defects as well as noise, and expect at least one
    that stops the compiler outright rather than merely warning.

13. Nothing in the repository asks a compiler or a static analyser to stop
    reporting something about these sources. No comment that exempts a line
    or a region from analysis, no pragma that silences a Clang or GCC
    diagnostic or presents a project header as the system's, no option that
    switches a warning off or demotes it. That covers a header the build
    generates as well, and every way of making one the system's: a pragma,
    a line marker, an include option or a build setting.
    A header the build forces into
    every translation unit is part of the repository for this purpose,
    wherever on disk it lives. Requirement 12 is answered by changing the
    code, not by narrowing what the compiler is allowed to say.

    The sanitizers are covered the same way. Nothing in the repository turns
    one off for any code, changes whether it stops at a report, sets its
    run-time options, calls into its run-time interface, or tests whether the
    build is instrumented. No such pragma, option, marker or sanitizer setting
    is written anywhere in the repository's sources, CMake files or scripts,
    not even in a comment that only mentions one.

### What must not change

14. The build still produces `libise.a` and the fifteen example programs,
    one per subdirectory of `examples/`, under exactly these names:
    `4in1_server`, `chargen_client`, `chargen_server`, `daytime_server`,
    `discard_server`, `echo_server`, `hello_world`, `http_server`,
    `max_connection`, `server_inspector`, `server_module_msgs`,
    `thread_pool`, `timer`, `udp_client`, `udp_server`. Fourteen of those
    names are their directory's; the exception is `examples/server_modules/`,
    which today builds `4in1_server` and keeps that name. Where under
    `build/` a target lands is the build system's business and is not fixed.

15. Every program behaves exactly as it does today. That means its command
    line (`--version`, `--help`, `--nodaemon`, and `chargen_client`'s
    positional address); the ports it listens on and the protocol it speaks
    there; what it writes to standard output; the log file it writes at
    `<directory of the executable>/log/<executable name>.log`, in its present
    format, line for line; its orderly shutdown on `SIGTERM`; and its refusal
    to start a second instance of the same executable.

    The servers daemonise by default. `--nodaemon` runs one in the
    foreground, which is how you watch one work.

16. The public API of the library — every type, function, constant and
    signature declared in `ise/main/*.h` — keeps compiling and keeps meaning
    what it means today. A caller written against today's headers, compiled
    as C++98 and linked against `libise.a`, must still compile, still link,
    and still get the same answers for the same arguments.

17. "As it does today" includes the places where today's behaviour is plainly
    wrong. A function that returns the wrong answer for an input it already
    accepts, an exception hierarchy a standard handler does not catch, a
    header that says one thing and does another: none of that is yours to
    correct in this milestone, and from the outside, correcting it and
    breaking it look the same. What you change here is what the compiler
    refuses or diagnoses, and you change it in the way that leaves every
    observable answer identical.

    One input is exempt, because there is nothing there to preserve:
    `repalceString` with an empty search pattern does not return.

18. The project supports platforms this environment is not. Its Windows
    implementation — `ise/main/ise_iocp.cpp`, and the `ISE_WINDOWS` and
    compiler-specific branches elsewhere — is never reached by anything built
    here, including by the warning set of requirement 11. That does not make
    it dead code. It stays, and it stays whole: an implementation emptied out
    because nothing here compiles it is a Windows port deleted, and the
    compiler-specific pragmas those branches already contain are part of what
    is preserved rather than instances of requirement 13.

## 2. Read these local references

Everything that defines this milestone is already in `/app`:

- **`build/linux/CMakeLists.txt` and `build/linux/build.sh`** — the build
  being replaced. They are the record of what the project needs in order to
  link and run: which sources go into the library, which examples are built,
  which system libraries are required. Read them before deleting them.
- **`examples/`** — one subdirectory per program of requirement 14, each
  with its own `CMakeLists.txt` naming the target and its sources.
  `examples/CMakeLists.txt` is the legacy build's list of them, and it is not
  complete.
- **`ise/main/*.h`** — the public API of requirement 16, and the only
  description of it.
- **`/opt/ise/toolchain-clang22.cmake`** — the environment's own toolchain
  description, referenced by requirement 9. It is outside `/app` and is not
  yours to edit.
- **`README`** and **`doc/`** — the project's own description of itself.

There is no specification document beyond these. The behaviour of
requirement 15 is defined by the code as it stands, so read the code.

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
  1, and notes on anything you changed in the sources and why.
