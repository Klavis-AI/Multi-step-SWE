
## Extension ranges: what `to max` resolves to

An extension range's `end` is **exclusive** (`DescriptorProto.ExtensionRange`,
`start` inclusive / `end` exclusive). The keyword `max` does **not** resolve to
the same number in every message:

| the message | `extensions 4 to max;` emits |
| --- | --- |
| an ordinary message | `end = 536870912` (2^29 — one past the largest field number) |
| a message with `option message_set_wire_format = true` | `end = 2147483647` (2^31−1) |

A MessageSet stores extensions as a repeated group keyed by an `int32` type id
rather than in the normal field-number space, so its extension numbers are not
bounded by the 2^29−1 field-number limit — and protoc records that by widening
the range's exclusive end.

Both values are what `protoc --descriptor_set_out` writes; a descriptor that uses
2^29 for a `message_set_wire_format` host differs from protoc's by exactly those
bytes and nothing else.
