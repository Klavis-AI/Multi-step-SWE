# Text format: what `--encode` and `--decode` must accept and produce

`protoc --encode=TYPE file.proto` reads text format on stdin and writes the
binary message to stdout. `--decode=TYPE` does the reverse. Both must match
protoc byte for byte.

## Ordering

Fields are emitted in **field-number order**, not declaration order. A message
declaring `b = 2` before `a = 1` prints `a` first.

## Extensions

An extension field is written under its **fully qualified name in brackets**:

    a: 5
    [pkg.e_int]: 42
    [pkg.e_str]: "hi"
    [pkg.Outer.e_nested]: 99

A message-valued extension takes the brace form, with no colon:

    [pkg.e_msg] {
      x: 7
    }

Extensions interleave with declared fields by field number: an extension
numbered 10 on a message whose declared fields are 50 and 51 prints first.

## `Any`

Inside a `google.protobuf.Any`, the expansion form names the payload type after
a type-URL prefix:

    a {
      [type.googleapis.com/pkg.Inner] { x: 9 }
    }

`--encode` must accept that form: resolve the type, encode the braced body as
that message, and store the URL in `type_url` and the bytes in `value`.
`--decode` prints `Any` in its unexpanded `type_url` / `value` form.

## Unknown fields

Recorded here for completeness; **no input in this step exercises it.** Every
input this step decodes was encoded against the same descriptor, so an unknown
field number cannot occur in those inputs. What follows describes the behaviour,
not a requirement — the requirement is the rule at the top of this file: match
`protoc` byte for byte.

A field number present on the wire but absent from the descriptor is printed by
number, in the same forms `--decode_raw` uses:

    99: 42                       varint
    100 { 13: 105 }              length-delimited that parses as a message
    101: "text"                  length-delimited that does not
    102: 0x00000001              fixed32
    103: 0x0000000000000001      fixed64
