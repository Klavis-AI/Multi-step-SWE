# gRPC over HTTP/2: what the interop suite exercises

`bin/grpc_server <port>` must listen on `127.0.0.1:<port>` and serve gRPC over
cleartext HTTP/2 on that port. Readiness is observed without touching the port:
the driver waits until the kernel reports a listening socket on `<port>`, so
nothing need be written to stdout and nothing connects to the server before the
interop client does. The client's connection is the first and only one the
server is given; it must serve that connection.

## Transport

- connection preface `PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n`, then SETTINGS
- frames: DATA, HEADERS, SETTINGS, PING, GOAWAY, WINDOW_UPDATE
- **HPACK** (RFC 7541): the static table, the dynamic table including indexed
  references, and **Huffman-coded** string literals. Real clients use Huffman.
- **flow control**: a peer may send only as much DATA as its window allows —
  65535 by default — and stops until WINDOW_UPDATE replenishes it. A server that
  never replenishes stalls mid-body and the request never completes.
- **frame size**: a DATA frame may not exceed SETTINGS_MAX_FRAME_SIZE (16384 by
  default). A larger frame is a connection error.
- **message reassembly**: one gRPC message may span several DATA frames, and one
  frame may carry several messages. The 5-byte length prefix and its body are
  split wherever HTTP/2 chooses.

## gRPC framing

Each message is `<1-byte compressed flag><4-byte big-endian length><body>`.
Responses carry `:status: 200` and `content-type: application/grpc` in initial
HEADERS, then DATA, then trailers with `grpc-status` (and `grpc-message` when
non-zero).

## The service

`grpc.testing.TestService`, driven by the upstream interop client:

| method | shape |
| --- | --- |
| `EmptyCall` | unary; empty request and reply |
| `StreamingInputCall` | client-streaming; **exactly one** response is owed, a `StreamingInputCallResponse{aggregated_payload_size = 1}` summing the `payload.body` lengths received |
| `UnaryCall` | unary; `SimpleRequest.response_status` (field 7, `EchoStatus{code=1,message=2}`) must be returned as the RPC status when set |

`SimpleResponse.payload` is field 1 wrapping `Payload`, whose `body` is field 2.
`StreamingInputCallRequest.payload` is field 1, also a `Payload`.
