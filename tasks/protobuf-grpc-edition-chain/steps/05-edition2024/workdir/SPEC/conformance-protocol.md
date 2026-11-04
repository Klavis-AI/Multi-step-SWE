# The conformance test protocol

Upstream's `conformance_test_runner` drives an implementation as a **child
process over pipes**. Your program does not know which tests exist, how many
there are, or what the expected answers are; it answers one request at a time
until stdin closes.

Upstream's own README says only that the program "needs to be able to read from
stdin and write to stdout" and points at `conformance.proto` for the messages.
The framing is not stated there, so it is stated here.

## Framing

Both directions use the same framing, on **stdin** and **stdout**:

    <uint32 length, little-endian, 4 bytes><that many bytes of serialized message>

* Read 4 bytes. Interpret them as an unsigned 32-bit little-endian length `n`.
* Read exactly `n` further bytes. They are a serialized `ConformanceRequest`.
* Write 4 bytes of little-endian length, then that many bytes of a serialized
  `ConformanceResponse`.
* Repeat. When a read of the 4-byte length hits **end of file**, the suite is
  over: exit 0.

Nothing else may go to stdout — it is the response channel. Diagnostics belong
on stderr.

## Messages

`proto/conformance.proto` is the authority for field numbers and names. In
outline, a `ConformanceRequest` carries:

* a payload, as one of `protobuf_payload` or `json_payload`;
* `requested_output_format`, saying which encoding the response must use;
* `message_type`, the fully-qualified name of the message the payload is an
  instance of;
* `test_category`.

A `ConformanceResponse` carries exactly one result: `protobuf_payload` or
`json_payload` on success, or one of `parse_error`, `serialize_error`,
`runtime_error`, `skipped`.

The distinction that matters: `parse_error` means *this input is invalid and I
correctly refused it*, and for a test whose input is deliberately malformed
that is the **passing** answer. `runtime_error` means your program failed. They
are not interchangeable.

## Schemas

Your program is invoked with a directory path as its first argument. That
directory contains the `.proto` files whose messages appear in requests:

    conformance.proto
    test_messages_proto2.proto
    test_messages_proto3.proto

Copies are in `proto/` here. Resolve `message_type` against these.

## Invocation

    conformance_test_runner [options] /app/bin/conformance <schema-dir>

Everything after the program path is passed to your program as arguments.
