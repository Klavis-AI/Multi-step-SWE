Protocol Buffers Language Specification (Proto2 Syntax) | Protocol Buffers Documentation
 Protocol Buffers Documentation

Protocol BuffersOverview
Protoc Installation
News
Programming GuidesLanguage Guide (editions)
Language Guide (proto 2)
Language Guide (proto 3)
Proto Limits
Style Guide
Enum Behavior
Encoding
ProtoJSON Format
Techniques
Add-ons
Extension Declarations
Field Presence
Symbol Visibility
Proto Serialization Is Not Canonical
Deserializing Debug Proto Representations

Protobuf EditionsOverview
Feature Settings for Editions
Implementing Editions Support

Design DecisionsNo Nullable Setters/Getters Support

Proto Best PracticesAvoid Cargo Culting
Proto Best Practices
1-1-1 Best Practice

TutorialsC++
C#
Dart
Go
Java
Kotlin
Python

Reference GuidesC++Generated Code Guide
String View APIs
Arena Allocation Guide
Abseil Support
C++ API

C#Generated Code Guide
C# API

DartGenerated Code
Dart API

GoGenerated Code Guide (Open)
Generated Code Guide (Opaque)
FAQ
Size Semantics
Go API
Opaque API Migration
Opaque API: Manual Migration
Opaque API FAQ

JavaGenerated Code Guide
UTF-8 Validation in Java
Generated Proto Names
Java API

KotlinKotlin
Generated Code Guide

Objective-CGenerated Code Guide

PHPGenerated Code Guide
PHP API

PythonGenerated Code Guide
Python Comparison
Python API

RubyGenerated Code Guide

RustGenerated Code Guide
Redaction in Rust
Building Rust Protos
Design Decisions

Protocol Buffers2023 Language Specification
Language Specification (Proto2 Syntax)
2024 Language Specification
Language Specification (Proto3)
Text Format Language Specification
MIME Types
Well-Known Types

Other Languages

SupportVersion Support
Debugging
Migration Guide
Cross-Version Runtime Guarantee

Downloads
History
Forum

Search Results

 View page source
 Edit this page
 Create child page
 Create documentation issue
 Create project issue
Lexical ElementsLetters and Digits
Identifiers
Integer Literals
Floating-point Literals
Boolean
String Literals
EmptyStatement
Constant

Syntax
Import Statement
Package
Option
FieldsNormal field
Group field
Oneof and oneof field
Map field

Extensions and ReservedExtensions
Reserved

Top Level definitionsEnum definition
Message definition
Extend
Service definition

Proto file

Reference Guides
Protocol Buffers
Language Specification (Proto2 Syntax)
Protocol Buffers Language Specification (Proto2 Syntax)
Language specification reference for the proto2 syntax and its relationship to Protobuf Editions.
Note: This language specification is known to be incomplete, particularly
regarding whitespace handling and other lexical nuances. The C++ protoc
compiler implementation serves as the definitive source of truth. This document
is a best-effort attempt to describe the behavior of the handwritten parser,
which is not based on a formal grammar.
The syntax is specified using
Extended Backus-Naur Form (EBNF):
| alternation
() grouping
[] option (zero or one time)
{} repetition (any number of times)

For more information about using proto2, see the
language guide.
Lexical Elements
Letters and Digits
letter = "A" ... "Z" | "a" ... "z"
capitalLetter = "A" ... "Z"
decimalDigit = "0" ... "9"
octalDigit = "0" ... "7"
hexDigit = "0" ... "9" | "A" ... "F" | "a" ... "f"

Identifiers
ident = letter { letter | decimalDigit | "_" }
fullIdent = ident { "." ident }
messageName = ident
enumName = ident
fieldName = ident
oneofName = ident
mapName = ident
serviceName = ident
rpcName = ident
streamName = ident
messageType = [ "." ] { ident "." } messageName
enumType = [ "." ] { ident "." } enumName
groupName = capitalLetter { letter | decimalDigit | "_" }

Integer Literals
intLit = decimalLit | octalLit | hexLit
decimalLit = [-] ( "1" ... "9" ) { decimalDigit }
octalLit = [-] "0" { octalDigit }
hexLit = [-] "0" ( "x" | "X" ) hexDigit { hexDigit }

Floating-point Literals
floatLit = [-] ( decimals "." [ decimals ] [ exponent ] | decimals exponent | "."decimals [ exponent ] ) | "inf" | "nan"
decimals = [-] decimalDigit { decimalDigit }
exponent = ( "e" | "E" ) [ "+" | "-" ] decimals

Boolean
boolLit = "true" | "false"

String Literals
strLit = strLitSingle { strLitSingle }
strLitSingle = ( "'" { charValue } "'" ) | ( '"' { charValue } '"' )
charValue = hexEscape | octEscape | charEscape | unicodeEscape | unicodeLongEscape | /[^\0\n\\]/
hexEscape = '\' ( "x" | "X" ) hexDigit [ hexDigit ]
octEscape = '\' octalDigit [ octalDigit [ octalDigit ] ]
charEscape = '\' ( "a" | "b" | "f" | "n" | "r" | "t" | "v" | '\' | "'" | '"' )
unicodeEscape = '\' "u" hexDigit hexDigit hexDigit hexDigit
unicodeLongEscape = '\' "U" ( "000" hexDigit hexDigit hexDigit hexDigit hexDigit |
 "0010" hexDigit hexDigit hexDigit hexDigit

EmptyStatement
emptyStatement = ";"

Constant
constant = fullIdent | ( [ "-" | "+" ] intLit ) | ( [ "-" | "+" ] floatLit ) |
 strLit | boolLit | MessageValue

MessageValue is defined in the
Text Format Language Specification.
Syntax
The syntax statement is used to define the protobuf version. If syntax is
omitted, the protocol compiler will use proto2. For the sake of clarity, it’s
recommended to always explicitly include a syntax statement in your .proto
files.
syntax = "syntax" "=" ("'" "proto2" "'" | '"' "proto2" '"') ";"
(Task note: protoc 32.1 leaves `FileDescriptorProto.syntax`, field 12, unset
for a proto2 file, whether the file says `syntax = "proto2"` or has no syntax
statement.)

Import Statement
The import statement is used to import another .proto’s definitions.
import = "import" [ "weak" | "public" ] strLit ";"

Example:
import public "other.proto";

Package
The package specifier can be used to prevent name clashes between protocol
message types.
package = "package" fullIdent ";"

Example:
package foo.bar;

Option
Options can be used in proto files, messages, enums and services. An option can
be a protobuf defined option or a custom option. For more information, see
Options in the
language guide.
option = "option" optionName "=" constant ";"
optionName = ( ident | bracedFullIdent ) { "." ( ident | bracedFullIdent ) }
bracedFullIdent = "(" ["."] fullIdent ")"

For examples:
option java_package = "com.example.foo";

Fields
Fields are the basic elements of a protocol buffer message. Fields can be normal
fields, group fields, oneof fields, or map fields. A field has a type, name, and
field number. In proto2, fields also have a label (required, optional, or
repeated).
label = "required" | "optional" | "repeated"
type = "double" | "float" | "int32" | "int64" | "uint32" | "uint64"
 | "sint32" | "sint64" | "fixed32" | "fixed64" | "sfixed32" | "sfixed64"
 | "bool" | "string" | "bytes" | messageType | enumType
fieldNumber = intLit;

Normal field
Each field has a type, name and field number. It may have field options. Note
that labels are optional only for oneof fields.
field = [ label ] type fieldName "=" fieldNumber [ "[" fieldOptions "]" ] ";"
fieldOptions = fieldOption { "," fieldOption }
fieldOption = optionName "=" constant

Examples:
optional foo.bar nested_message = 2;
repeated int32 samples = 4 [packed=true];

Group field
Note that this feature is deprecated and should not be used when creating new
message types. Use nested message types instead.
Groups are one way to nest information in message definitions. The group name
must begin with capital letter.
group = label "group" groupName "=" fieldNumber messageBody

Example:
repeated group Result = 1 {
 required string url = 1;
 optional string title = 2;
 repeated string snippets = 3;
}

Oneof and oneof field
A oneof consists of oneof fields and a oneof name. Oneof fields do not have
labels.
oneof = "oneof" oneofName "{" { option | oneofField } "}"
oneofField = type fieldName "=" fieldNumber [ "[" fieldOptions "]" ] ";"
(Task note: protoc 32.1 also accepts a group without a label as a oneof
member, `group Name = N { ... }`; the field it synthesises has type
TYPE_GROUP and carries that oneof's `oneof_index`. A group with a label
inside a oneof is refused, like any labelled oneof field.)

Example:
oneof foo {
 string name = 4;
 SubMessage sub_message = 9;
}

Map field
A map field has a key type, value type, name, and field number. The key type can
be any integral or string type. Note, the key type may not be an enum.
mapField = "map" "<" keyType "," type ">" mapName "=" fieldNumber [ "[" fieldOptions "]" ] ";"
keyType = "int32" | "int64" | "uint32" | "uint64" | "sint32" | "sint64" |
 "fixed32" | "fixed64" | "sfixed32" | "sfixed64" | "bool" | "string"

Example:
map<string, Project> projects = 3;

Extensions and Reserved
Extensions and reserved are message elements that declare a range of field
numbers or field names.
Extensions
Extensions declare that a range of field numbers in a message are available for
third-party extensions. Other people can declare new fields for your message
type with those numeric tags in their own .proto files without having to edit
the original file.
extensions = "extensions" ranges ";"
ranges = range { "," range }
range = intLit [ "to" ( intLit | "max" ) ]

Examples:
extensions 100 to 199;
extensions 4, 20 to max;

For more on this topic, see
Extension Declarations.
Reserved
Reserved declares a range of field numbers or field names in a message that can
not be used.
reserved = "reserved" ( ranges | strFieldNames ) ";"
strFieldNames = strFieldName { "," strFieldName }
strFieldName = "'" fieldName "'" | '"' fieldName '"'

Examples:
reserved 2, 15, 9 to 11;
reserved "foo", "bar";

Top Level definitions
Enum definition
The enum definition consists of a name and an enum body. The enum body can have
options, enum fields, and reserved statements.
enum = "enum" enumName enumBody
enumBody = "{" { option | enumField | emptyStatement | reserved } "}"
enumField = ident "=" [ "-" ] intLit [ "[" enumValueOption { "," enumValueOption } "]" ]";"
enumValueOption = optionName "=" constant

Example:
enum EnumAllowingAlias {
 option allow_alias = true;
 EAA_UNSPECIFIED = 0;
 EAA_STARTED = 1;
 EAA_RUNNING = 2 [(custom_option) = "hello world"];
}

Message definition
A message consists of a message name and a message body. The message body can
have fields, nested enum definitions, nested message definitions, extend
statements, extensions, groups, options, oneofs, map fields, and reserved
statements. A message cannot contain two fields with the same name in the same
message schema.
message = "message" messageName messageBody
messageBody = "{" { field | enum | message | extend | extensions | group |
option | oneof | mapField | reserved | emptyStatement } "}"

Example:
message Outer {
 option (my_option).a = true;
 message Inner { // Level 2
 required int64 ival = 1;
 }
 map<int32, string> my_map = 2;
 extensions 20 to 30;
}

None of the entities declared inside a message may have conflicting names. All
of the following are prohibited:
message MyMessage {
 optional string foo = 1;
 message foo {}
}

message MyMessage {
 optional string foo = 1;
 oneof foo {
 string bar = 2;
 }
}

message MyMessage {
 optional string foo = 1;
 extend Extendable {
 optional string foo = 2;
 }
}

message MyMessage {
 optional string foo = 1;
 enum E {
 foo = 0;
 }
}

Extend
If a message in the same or imported .proto file has reserved a range for
extensions, the message can be extended.
extend = "extend" messageType "{" {field | group} "}"

Example:
extend Foo {
 optional int32 bar = 126;
}

Service definition
service = "service" serviceName "{" { option | rpc | emptyStatement } "}"
rpc = "rpc" rpcName "(" [ "stream" ] messageType ")" "returns" "(" [ "stream" ]
messageType ")" (( "{" { option | emptyStatement } "}" ) | ";" )

Example:
service SearchService {
 rpc Search (SearchRequest) returns (SearchResponse);
}

Proto file
proto = [syntax] { import | package | option | topLevelDef | emptyStatement }
topLevelDef = message | enum | extend | service

An example .proto file:
syntax = "proto2";
import public "other.proto";
option java_package = "com.example.foo";
enum EnumAllowingAlias {
 option allow_alias = true;
 EAA_UNSPECIFIED = 0;
 EAA_STARTED = 1;
 EAA_RUNNING = 1;
 EAA_FINISHED = 2 [(custom_option) = "hello world"];
}
message Outer {
 option (my_option).a = true;
 message Inner { // Level 2
 required int64 ival = 1;
 }
 repeated Inner inner_message = 2;
 optional EnumAllowingAlias enum_field = 3;
 map<int32, string> my_map = 4;
 extensions 20 to 30;
}
message Foo {
 optional group GroupMessage = 1 {
 optional bool a = 1;
 }
}

© 2026 Google LLC All Rights Reserved
Privacy Policy
Hosted by GitHub Pages. GitHub Privacy Statement