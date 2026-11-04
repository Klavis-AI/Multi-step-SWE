# gRPC over HTTP/2

Your project already compiles Protocol Buffers and speaks the wire, JSON and text
formats. This step adds the other artifact the interface calls for: **a gRPC
implementation over HTTP/2**, written the same way as everything else.

The project remains your own implementation: it must not contain, link, vendor,
download or invoke a third-party Protocol Buffers, gRPC or HTTP/2 library. The
toolchain available is GCC with the C standard library and zlib. Your binaries
may link only libc, libm, libz and libgcc_s. All compiled code in the project is C; no file anywhere under `/app` may be
C++ object code or carry the C++ runtime.

`make` at `/app` must still produce `/app/bin/protoc` and `/app/bin/conformance`.
`make grpc` must additionally produce `/app/bin/grpc_server` and
`/app/bin/grpc_client`.
  (`bin/conformance` and `bin/grpc_client` are part of the build contract; at
  this step the server is driven by an upstream client, not by your own.)

## The server

`bin/grpc_server <port>` listens on `127.0.0.1:<port>` and serves cleartext
HTTP/2. It is driven by the **upstream gRPC interop client** — an
implementation you did not write — over `grpc.testing.TestService`:

Readiness is observed without touching the port: the client is started once the
kernel reports a listening socket on `<port>`. Nothing is required on stdout,
and nothing connects to your server before the interop client does — its
connection is the first and only one the server receives.

- `EmptyCall` — unary, empty request and reply
- `StreamingInputCall` — client-streaming; exactly one response, carrying the
  summed `payload.body` lengths
- `UnaryCall` with `response_status` — the requested status code and message must
  come back as the RPC status

A fourth method is exercised by the interop client's status case and must be served:

- `FullDuplexCall` — bidirectional streaming over
  `/grpc.testing.TestService/FullDuplexCall`. A
  `StreamingOutputCallRequest` carries `response_status` as **field 7**
  (`EchoStatus{code = 1, message = 2}`), and when it is set the call must
  terminate with that status and no reply body. The upstream interop client
  opens this stream before it checks the unary path, so a server that answers
  only the three methods above fails the status case outright.

Nothing here is satisfied by a server that merely accepts a connection. Real
clients Huffman-encode their headers, send bodies larger than one flow-control
window, and split messages across frames. `SPEC/grpc-http2.md` states the
requirements precisely. The documents it builds on are in the image: HTTP/2
(RFC 9113, local copy: `/opt/docs/rfc/rfc9113.txt`), HPACK (RFC 7541, local copy:
`/opt/docs/rfc/rfc7541.txt`), and gRPC's protocol and status-code documents
(local copies: `/opt/docs/grpc/PROTOCOL-HTTP2.md`, `/opt/docs/grpc/statuscodes.md`).

## What must keep working

Everything earlier. The encode, decode, `--decode_raw` and text-format
behaviour of every proto2, proto3 and editions file you already
handle must keep working.

## Meta requirements

- You have **7,200 seconds** for this step. Run `bash /app/timer.sh` to see the
  remaining budget. The configured timeout is authoritative.
- Keep your source under `/app`; it is the deliverable.
- The project must build from source in a clean checkout with none of your
  build products present, in a fresh container with the toolchain in this
  image, and its programs must work through the command lines above.
  That build is plain `make` at `/app`, no arguments, under a **900-second**
  limit, followed by `make grpc` under a **300-second** limit, and every program
  it must produce has to be an ELF executable that build wrote — not a script,
  and not a binary the tree shipped.
- Do not cheat by using online solutions or hints specific to this task.
- Work only from this workspace and the documentation named above; do not
  search the system for other implementations or answers.
- Check your own progress by running your programs and comparing against what
  the specifications above require.
