# Step 10 — the connection protocol stops being a state machine

## 1. Implement this milestone

ISE (Iris Server Engine) is a C++ server framework: one static library and
fifteen example programs that demonstrate it. It is a CMake project — `cmake
-S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build` configures
and builds all of it — it is compiled as C++20, and since the last milestone
its interface is delivered as named modules.

Everything C++20 gave this codebase so far was something it could have been
written with. This one is not. A TCP session in ISE is a conversation: send a
banner, read a line, answer it, read the next one. The library has no way to
say that. `TcpConnection::send()` and `TcpConnection::recv()` post a task and
return immediately; when the task finishes, the event loop calls a *different*
function on the business object — `onTcpSendComplete()` or
`onTcpRecvComplete()` — with nothing but the connection and an opaque
`Context` the caller had to pack itself. The function that knew what it was
doing has already returned. Its local variables are gone. Whatever it needs
to remember has to be written down somewhere that outlives it.

So it is, everywhere. `examples/echo_server` is four callbacks whose only
content is which one runs next. `ise/main/ise_http.cpp` is worse and more
honest about it: an `enum class RecvReqState` with four states, an `enum class
SendResState` with two, a heap-allocated `ConnContext` holding both of them
plus the request, the response and two content streams, attached to every
connection, and two callbacks that switch on the states and advance them. Six
states, one heap object per connection, and one linear HTTP/1.0 exchange
smeared across them.

C++20 coroutines are the replacement. A coroutine suspends *inside* a
function, keeping its locals, and resumes there when the thing it was waiting
for happens. The conversation becomes the function that reads like the
conversation: the banner, the loop, the answer, in that order, with the state
in ordinary local variables where the compiler can see it.

This milestone makes a TCP session one coroutine, and makes the transfer
operations awaitable.

### What this milestone is not

* **It is not a new I/O engine.** The epoll event loop, the send and receive
  task queues, the packet splitters, the buffers, the timeout checking and
  the threading model are exactly what they are today. What changes is who
  gets told that a task finished and how they remember why they asked.
* **It is not a general async framework.** There is no scheduler, no
  thread-pool executor, no `sync_wait`, no when_all. A coroutine here is
  resumed by the event loop thread that completed its transfer, on that
  thread, exactly as the callback it replaces was called.
* **It is not a rewrite of the UDP or assistor paths.** `onRecvedUdpPacket`,
  the assistor threads and the daemon thread keep their present shapes.

### The six requirements

1. **A TCP session is one coroutine.** `TcpCallbacks` — the interface in
   `ise/main/ise_application.h` that `IseBusiness` and `IseServerModule`
   implement — loses `onTcpConnected`, `onTcpRecvComplete` and
   `onTcpSendComplete`, and gains one function in their place:

   ```c++
   virtual ConnectionTask onTcpConnection(TcpConnectionPtr connection) = 0;
   virtual void onTcpDisconnected(const TcpConnectionPtr& connection) = 0;
   ```

   `onTcpDisconnected` stays as it is: it is not part of a conversation, it
   is the report that one ended.

   `ConnectionTask` is the return type of a coroutine — a class with a
   `promise_type` — that you define, in a header of your own under
   `ise/main/`. It is **detached**: the framework calls the session function
   once, when the connection is accepted, and discards what comes back. The
   coroutine owns its own frame and destroys it when it runs off the end.
   Nothing waits for it, so `ConnectionTask` needs no `await_ready` and no
   continuation slot; what it does need is a `initial_suspend` that does not
   suspend (the session starts running the moment the connection arrives,
   exactly as `onTcpConnected` did), a `final_suspend` that does not suspend
   (nobody is going to destroy the frame for it), `return_void`, and an
   `unhandled_exception` that does something defensible — an exception
   cannot propagate out of a resume into the event loop, because on the
   event loop there is no caller of the business object left to catch it.

2. **`send` and `recv` are awaitable.** `TcpConnection::send` and
   `TcpConnection::recv` stop returning `void` and start returning awaiters.
   `SendAwaiter` and `RecvAwaiter` are members of `TcpConnection`: what they
   suspend on is that connection's task deques, and a type whose whole
   meaning is one connection's pending transfer belongs inside it. Their
   results cannot be dropped:

   ```c++
   [[nodiscard]] SendAwaiter send(std::span<const std::byte> data,
                                  int timeout = TIMEOUT_INFINITE);
   [[nodiscard]] RecvAwaiter recv(const PacketSplitter& packetSplitter = ANY_PACKET_SPLITTER,
                                  int timeout = TIMEOUT_INFINITE);
   ```

   The `const Context&` parameter comes off both. It was there so that a
   caller could label a transfer and recognise its own label in the
   completion callback; a coroutine does not need to be told what it was
   doing, because it never stopped doing it.

   `co_await connection->send(...)` yields `bool`, whether the data went
   out. `co_await connection->recv(...)` yields the packet, as the
   `std::span<const std::byte>` that `onTcpRecvComplete` used to be handed,
   borrowed from the same receive buffer with the same lifetime: valid until
   the awaiting coroutine next suspends. Those two answers are also how a
   session recognises a transfer that was completed because the connection
   failed, timed out or was closed at shutdown, which is still a completion:
   `false`, and a span of length zero.

   The `Context` inside the send and receive *tasks* stays exactly where it
   is. That is the mechanism this requirement runs on: what the task carries
   now is the suspended coroutine, so `await_suspend` posts the task with
   the handle in the `Context` and the completion path resumes it.

3. **Every suspended coroutine is resumed exactly once.** A session that is
   waiting on a transfer holds a live frame, and a frame that is never
   resumed is never destroyed.

   * The session function takes its `TcpConnectionPtr` **by value**, so a
     suspended session keeps the connection alive by itself. The connection
     must not hold the coroutine: that is a cycle and neither end is freed.
   * When a connection fails or is closed — `errorOccurred()`, a timeout,
     `clearConnections()` at shutdown — every task still queued on it is
     completed rather than dropped. A session woken this way must be able to
     tell that it was: `co_await ...->send(...)` yields false, and
     `co_await ...->recv(...)` yields an empty span. Today this cannot
     happen at all, because `onTcpRecvComplete` is simply not called for a
     transfer that never completed; after this milestone every await returns
     and "empty" is how the conversation ends.
   * A resumption runs arbitrary business code, and that code may post a new
     transfer on the same connection. The completion paths in
     `ise/main/ise_server_tcp.cpp` hold references into the task deques
     while they call out; make sure a resume cannot invalidate what its
     caller is standing on.

4. **The HTTP server's state machine is deleted, not ported.**
   `ise/main/ise_http.h` declares `enum class RecvReqState`, `enum class
   SendResState`, `struct ConnContext` and `ConnContextPtr`, and
   `ise/main/ise_http.cpp` implements `HttpServer` as four callbacks
   switching on them. After this milestone those four names do not exist and
   `HttpServer::onTcpConnection` is the HTTP/1.0 exchange written in order:
   request line, headers, body, the `onHttpSession_` hook, response headers,
   response body, disconnect. The states become control flow and the
   `ConnContext` members become locals of the coroutine.

   Deleting four names is not the requirement; deleting the machine is. An
   enumeration of where in the conversation a connection has got to is that
   machine whatever it is called, and a coroutine has no use for one — where
   it has got to is where its instruction pointer is. So after this
   milestone the HTTP server declares no enumeration of its own. The three
   protocol vocabularies at namespace scope in `ise/main/ise_http.h` —
   `HTTP_PROTO_VER`, `HTTP_METHOD_TYPE`, `HTTP_NEXT_OP` — are not state
   machines and stay exactly as they are.

5. **The response body is produced by a generator.** Sending a response body
   is "read a block from the content stream, send it, come back for the next
   one" — the other place where a loop was turned inside out. Write a
   `Generator<T>` in the same header as `ConnectionTask`: a class with a
   `promise_type` that suspends on `co_yield`, exposing `begin()`/`end()` so
   that the body loop is a range-`for` over the blocks. Use it for the
   response body: the coroutine that yields the body's blocks is a member
   function of `HttpServer`.

   `Generator<T>`'s iterator is single-pass — an `input_iterator` over a
   coroutine that cannot be restarted — and the type is move-only, because
   two objects owning one frame would destroy it twice.

6. **The examples are sessions.** Every program under `examples/` that
   implements TCP callbacks, and every `ServerModule_*` under
   `examples/server_modules/`, is rewritten to the new interface: one
   `onTcpConnection` per business object, written in the order its
   conversation happens.

   Two of them are the reason this is a requirement and not a consequence.
   `examples/chargen_server` and `examples/server_modules/svr_mod_chargen`
   have *two* chains on one connection — an endless send and an endless
   discarding receive — which is why they are the two that were hardest to
   read as callbacks. One coroutine cannot await two things at once, and it
   does not need to: start a second `ConnectionTask` on the same connection.
   Both hold the connection by value and the connection lives until both are
   finished.

   `examples/hello_world` is the other end of the range: two callbacks whose
   entire content was "send this, then hang up" become two statements.

## 2. What must not change

1. **The wire.** Every program still speaks the protocol it speaks today, on
   the port it speaks it on, byte for byte: the echo server's banner and its
   `quit`, chargen's 94-line ring, daytime's timestamp and immediate close,
   discard's silence, the HTTP server's status line, headers and body. What
   this milestone changes is where the code that produces those bytes lives,
   not the bytes.

2. **The log.** Every program still writes `<directory of the executable>/log/
   <executable name>.log`, and every line it wrote before is still written,
   for the same event, with the same text. The log is output, not structure:
   the examples' `onTcpConnected`, `onTcpRecvComplete` and
   `onTcpSendComplete` lines still appear once per connection, once per
   completed receive and once per completed send, with exactly that wording,
   even though the three functions that used to emit them are gone. Renaming
   a function is the migration; renaming what it prints is a change to the
   program. A line whose emitting callback disappears is written where that
   suspension now returns, and only where the callback would have run — the
   callbacks fire on completion, so a receive that ends in a timeout or a
   closed connection logs nothing, exactly as it logs nothing today.

3. **The rest of the public API.** Everything `ise/main/*.h` declares that
   this milestone does not name is declared after your change with the same
   name, the same signature, the same default arguments and the same
   semantics. `TcpConnection`'s own members other than `send` and `recv` —
   `disconnect`, `shutdown`, `setNoDelay`, `getConnectionName`,
   `getPeerAddr`, `getServerConnCount`, `getServerIndex`, `isConnected` —
   are untouched, and so are `TcpServer`, `TcpEventLoop`,
   `BaseTcpConnection`, `IseApplication`, `Timer`, `ThreadPool`, and every
   free function in `ise_sys_utils.h`.

4. **Everything the earlier milestones established.** The tree you inherit is
   the output of nine milestones, and everything they established still
   holds. None of it is new work; it is here because this milestone changes
   the shape of the library's central interface, deletes three virtual
   functions, changes the return type of two more, rewrites the HTTP server
   and rewrites ten example programs — and a rule that was true of the code
   being replaced is true of the code that replaces it. A coroutine is a
   function in this project like any other, and every rule that applied to a
   function applies to it.

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
   * **The module structure.** The library's declarations reach their
     consumers through named modules: the interface units are declared to the
     build as the library's own `PUBLIC` C++ module file set, every `.cpp`
     under `ise/main/` imports rather than includes a project header, the
     units import each other rather than re-declaring, and the declarations
     an interface unit exports stay attached to the global module so that the
     examples' textual includes still name the same entities. Every new file
     you add is subject to that: a header of yours under `ise/main/` is part
     of the library's interface and therefore belongs to a named module and
     is reachable through `import ise;` exactly as the twenty-one that are
     there already are, and a new `.cpp` in the library imports rather than
     includes. `ise/main/ise_options.h`, `ise_global_defs.h`, `ise_stldefs.h`,
     `ise_assert.h` and `ise_template.h` stay textual.
   * **Ownership.** No delete-expression anywhere in the sources this
     environment compiles; every new-expression's result goes straight into an
     owning handle or into a `return` that hands ownership over; Boost owns
     nothing; run under the compiler's allocation checking, every program
     reaches exit with nothing still allocated and unreachable, and reports no
     use after free, no double release and no release of something never
     allocated. The ownership is spelled in the standard's words: the
     project's own objects are held by `std::unique_ptr` where ownership is
     exclusive and by `std::shared_ptr` where it is shared. Requirement 3 of
     section 1 is that rule applied to a coroutine frame, which is an
     allocation like any other and is the one this milestone adds.
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
     tree. `Generator<T>` is a class template this project declares, so it
     states what it requires of `T` — a template written at this milestone is
     not an exception to the rule the concepts milestone set.
   * **The library vocabulary of the C++20 milestones.** `Logger::writeFmt`
     takes a `std::format_string` and composes its message with
     `std::format`, not with `std::vformat` and not from a `const char *`;
     `DateTime`, `Timestamp` and `InetAddress` each default their `<=>`
     and their `==` rather than writing the comparisons out; and the eleven
     hand-written searches are ranges algorithms.
   * **A buffer carries its own length.** None of `TcpConnection::send`,
     `UdpCallbacks::classifyUdpPacket`, `UdpPacket::setPacketBuffer`,
     `UdpSocket::sendBuffer` or `MainUdpServer::onRecvData` takes a `void *`,
     and a buffer crossing the socket, server, server-module and application
     interfaces is a `std::span` rather than a pointer beside a length
     (`Buffer`, the streams, `Packet`, the packet splitters,
     `IoBuffer::append` and the HTTP client keep the pointer-and-length
     members they have). `UdpPacket` hands out its memory
     through the one accessor `getPacket()`, and neither `getPacketBuffer`
     nor `getPacketSize` appears in these sources. The sixth name on that list used
     to be `TcpCallbacks::onTcpRecvComplete`, which this milestone deletes.
     The requirement did not go with it: the received bytes now come back out
     of the awaiter that `TcpConnection::recv` returns, and they come back as
     one value that carries its own length. Requirement 2 of section 1 is
     where that lands.

5. **The examples are still callers.** They keep `#include
   "ise/main/ise.h"`, gain no `import`, become no module, and link against
   the same library. `examples/udp_server`, `examples/udp_client`,
   `examples/timer`, `examples/thread_pool` and
   `examples/server_module_msgs` do not implement TCP callbacks and have no
   business changing at all.

6. **The frozen platform branches.** The `#ifdef ISE_WINDOWS` arms of these
   sources are not compiled in this environment and are not yours to edit,
   including `ise/main/ise_iocp.cpp` in its entirety. The IOCP completion
   path calls the callbacks this milestone removes; it is frozen, it is not
   compiled here, and porting it is not part of this task. Leave it exactly
   as it is.

7. **No diagnostic is suppressed.** No `#pragma` that disables a warning, no
   `-Wno-`, no `NOLINT`, no `-fpermissive`, and no `(void)x;` added to
   silence one. A coroutine that the compiler warns about — a parameter
   captured by reference, a dangling `co_await` operand, an unused result —
   is the compiler telling you something true about requirement 3.

   Nothing presents a header of the project, or one the build
   generates, as a system header: no pragma, line marker, include
   option or build setting does that.

   The sanitizers are covered the same way. Nothing in the repository turns
   one off for any code, changes whether it stops at a report, sets its
   run-time options, calls into its run-time interface, or tests whether the
   build is instrumented. No such pragma, option, marker or sanitizer setting
   is written anywhere in the repository's sources, CMake files or scripts,
   not even in a comment that only mentions one.

8. **Nothing is passed to the compiler out of band.** No `-w`, no
   `--config`, no `-march=native`, no flags injected through the environment
   or a launcher. The only response file on any compile is the module map
   the generator produces, and what is inside it counts exactly as if it
    were spelled on the command line.

9. **The build.** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake
   --build build`, from a clean tree, configures and builds the library and
   all fifteen examples with no diagnostics. `libise.a` in the same place,
   the same fifteen executables in the same places with the same names.

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

10. **The places today's behaviour is plainly wrong.** As in every earlier
    milestone, "as it does today" includes those, and correcting the answer
    the project gives is not part of this one. This step has a trap of that
    shape big enough to name, because the obvious rewrite walks straight into
    it: **`HttpServer`'s request-body loop**. `RRS_RECVING_CONTENT` tests
    whether it has enough content *before* writing the packet it has just
    received, so a body that arrives in a single packet always waits for one
    packet more than the client will send; and on the receive where that test
    does pass, the state machine sets `RRS_COMPLETE`, returns, and neither
    dispatches that state nor posts another receive, so the connection hangs
    until it times out. A POST to this server has never been answered. The
    natural coroutine —
    `while (stream.getSize() < contentLength) { recv; write; }` — terminates
    correctly and replies `200 OK`, which is a different program. Reproduce
    the order the state machine uses, not the order that works.

    If you keep a bug deliberately, say so in the handoff notes so the next
    session knows it was read and kept rather than missed.

## 3. Read these local references

Everything that defines this milestone is already in `/app`:

- **`ise/main/ise_server_tcp.h`** — `TcpConnection`, the `SendTask` and
  `RecvTask` structures with the `Context` that requirement 2 reuses, the
  packet splitters, and the header comment that describes the connection
  lifetime you must not break.
- **`ise/main/ise_server_tcp.cpp`** — `LinuxTcpConnection::trySend` and
  `tryRetrievePacket`, the two places a transfer completes; `errorOccurred`,
  `checkTimeout` and `TcpEventLoop::clearConnections`, the three places one
  does not; and the frozen `#ifdef ISE_WINDOWS` block.
- **`ise/main/ise_application.h`** — `TcpCallbacks`, `IseBusiness` and the
  default implementations the examples inherit.
- **`ise/main/ise_global_defs.h`** — `Context` (`std::any`),
  `EMPTY_CONTEXT`, `asBytes`/`asWritableBytes`.
- **`ise/main/ise_http.h`** and **`ise_http.cpp`** — requirement 4's state
  machine and requirement 5's content streams.
- **`ise/main/ise_svr_mod.h`**, **`ise_svr_mod.cpp`** and
  **`ise_inspector.h`**, **`ise_inspector.cpp`** — the other two
  implementations of `TcpCallbacks` in the library, one dispatching to
  modules and one forwarding to the HTTP server.
- **`ise/main/ise.cppm`** and **`ise/main/ise.*.cppm`** — where a new header
  of yours has to appear for requirement 4 of section 2.
- **`CMakeLists.txt`** — the `ise` target and its `FILE_SET CXX_MODULES`.
- **`examples/`** — the fifteen programs, ten of which implement TCP
  callbacks.
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
