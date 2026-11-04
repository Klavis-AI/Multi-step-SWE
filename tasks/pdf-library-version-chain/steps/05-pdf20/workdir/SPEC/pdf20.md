# PDF 2.0

Normative text: **ISO 32000-2:2020** (PDF 2.0). **That document is not shipped in this image**,
so this one is the authoritative statement of it for this workspace, in the same way
`/app/SPEC/pdf16-17.md` states ISO 32000-1 for the previous milestone.

`/opt/pdfspec/ISO-32000-1-2008.txt` holds the published ISO 32000-1:2008 text. PDF 2.0
**revises and extends** that specification rather than replacing it: every structure it does
not touch is still defined there, and the clause and table numbers used below are ISO
32000-1's wherever the structure predates 2.0, marked as such. Where 2.0 changes a rule, the
change is stated here and the 1.7 rule it replaces is stated with it, so that the difference
is visible rather than implied.

The previous milestones' clauses, in `/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md` and
`pdf16-17.md`, still apply in full. This document adds to them and replaces only what it says
it replaces.

The header version is not a gate on which structures may appear. A file declaring 1.7 that
uses every structure below shall be read exactly as one declaring 2.0, and a file declaring
2.0 that uses only 1.3 structures is an ordinary file. One operation below — writing a
decrypted copy — *produces* a file; everything else is reading.

## 1. Encryption at `/V 5` — AES-256 and revision 6

### 1.1 What changes, and what does not

ISO 32000-1's clause 7.6 defines the standard security handler through `/V 4` and `/R 4`, and
`/app/SPEC/pdf16-17.md` §1 states it. All of that still holds. PDF 2.0 adds one value of `/V`
and one of `/R`, and the additions are not variations on the earlier scheme — the key
derivation, the password check, the per-object key rule and the dictionary's entries are all
different:

| | `/V` 1, 2, 4 (`/R` 2, 3, 4) | `/V 5` (`/R 5`, `/R 6`) |
| --- | --- | --- |
| cipher | RC4, or AES-128 as `/AESV2` | AES-256 as `/AESV3` |
| file key length | 40 to 128 bits, from `/Length` | always 256 bits |
| key for one object | derived per object (Algorithm 1) | **the file key itself, unmodified** |
| `/O`, `/U` | 32 bytes | 48 bytes, carrying two salts each |
| key storage | none; the key is re-derived | `/UE` and `/OE`, 32 bytes each |
| permissions check | none | `/Perms`, 16 bytes, cross-checked |
| password hash | MD5, with 50 rounds at `/R` 3+ | SHA-2, Algorithm 2.B |

What is unchanged: `/Encrypt` in the trailer is what makes a document encrypted and nothing
else may be taken as evidence either way; the encryption dictionary's own strings are never
encrypted; `/ID` is never encrypted; a string inside an already-encrypted stream is not
encrypted a second time; and `/P`, `/EncryptMetadata`, `/StmF`, `/StrF`, `/CF` and the
`/Identity` crypt filter keep the meanings ISO 32000-1 gives them.

### 1.2 The encryption dictionary at `/V 5` — 7.6.3.2, table 21 as 2.0 extends it

* `/Filter` — `/Standard`, as before.
* `/V` — **5**: the handler uses crypt filters, and the algorithm is named in `/CF`.
* `/R` — **6** for ISO 32000-2. **5** is the earlier, deprecated form of the same scheme (it
  was published as an Adobe extension level 3) and differs from 6 in the password hash alone,
  as 1.6 sets out. Both shall be read; a file declaring `/R 5` shall not be refused.
* `/Length` — 256, in **bits**, as in every earlier revision.
* `/CF` — required: `/StdCF` with `/CFM /AESV3`, `/AuthEvent /DocOpen` and `/Length` **32**,
  in bytes, as the crypt filter dictionary has always expressed it (7.6.5, table 25).
* `/StmF`, `/StrF` — `/StdCF`, or `/Identity` where that data is not encrypted.
* `/O`, `/U` — required, **48 bytes each**, laid out in 1.4.
* `/OE`, `/UE` — required, **32 bytes each**: the file encryption key, wrapped.
* `/Perms` — required, **16 bytes**: `/P` and `/EncryptMetadata`, encrypted (1.7).
* `/P` — the permission flags, unchanged in meaning and still written as a negative integer.
* `/EncryptMetadata` — unchanged in meaning, and now also recorded inside `/Perms`.

A `/V 5` dictionary that lacks `/UE`, `/OE` or `/Perms` is **malformed**. It is not a document
that happens to be unreadable and it is not an unencrypted document: a reader shall report it
as a broken encryption dictionary rather than fall back to reading the file in the clear.

### 1.3 Applying AES-256 — the per-object rule that changes

At `/V 5` the file encryption key is used **directly**, for every string and every stream in
the document. The object and generation numbers are not mixed into it, the four extra bytes
`73 41 6C 54` that `/AESV2` appends are not appended, and no MD5 is computed: Algorithm 1 of
ISO 32000-1 7.6.2 **does not apply at this revision**. A reader that derives a per-object key
here decrypts nothing.

Everything else about applying the cipher is as `/app/SPEC/pdf16-17.md` §1.2 states for
`/AESV2`: AES in Cipher Block Chaining mode, the 16-byte initialization vector stored as **the
first 16 bytes** of the encrypted stream or string, and PKCS #5 padding on the plaintext,
removed on decryption. The key is 32 bytes rather than 16. Stream data is encrypted after all
encoding filters have been applied, so a reader decrypts and then decodes, and the stream
dictionary's `/Length` counts the encrypted bytes.

### 1.4 The layout of `/U` and `/O`

Each is 48 bytes in three fields:

```
bytes  0..31   a 32-byte hash
bytes 32..39   the 8-byte validation salt
bytes 40..47   the 8-byte key salt
```

`/U` is computed from the user password alone. `/O` is computed from the owner password
**and the whole 48 bytes of `/U`**, which is why `/U` shall be complete before `/O` is
computed and why a change to `/U` invalidates `/O`.

`/UE` holds the file encryption key encrypted with AES-256 in CBC mode, **no padding and an
initialization vector of 16 zero bytes**, under a key derived from the user password and the
user key salt. `/OE` is the same under a key derived from the owner password, the owner key
salt and `/U`.

### 1.5 Algorithm 2.A — authenticating a password and recovering the file key

a) The password is a Unicode string. It shall be prepared with the SASLprep profile of
   stringprep and encoded as UTF-8, and **truncated to 127 bytes** if longer. An empty
   password is the empty byte string — it is *not* padded with the 32-byte padding string the
   earlier revisions use, which has no role at this revision.
b) **Test it as the user password:** hash it (1.6) with the user validation salt, `/U` bytes
   32 to 39, and no additional data. If the result equals `/U` bytes 0 to 31, the password is
   the user password.
c) **Test it as the owner password:** hash it with the owner validation salt, `/O` bytes 32 to
   39, and the 48 bytes of `/U` as additional data. If the result equals `/O` bytes 0 to 31,
   the password is the owner password.
d) **From the user password:** hash it with the user key salt, `/U` bytes 40 to 47, and no
   additional data; decrypt `/UE` with that 32-byte key, AES-256 CBC, no padding, zero
   initialization vector. The 32 bytes that come out are the file encryption key.
e) **From the owner password:** hash it with the owner key salt, `/O` bytes 40 to 47, and the
   48 bytes of `/U`; decrypt `/OE` the same way.
f) Check `/Perms` (1.7).

A reader shall try the **empty user password** first, so that a document whose user password
is empty opens with nothing supplied. Where that fails, either password shall open the
document when it is given, and which one was given determines what the reader may then permit.
A document that authenticates under neither shall fail rather than return partly decrypted
data: at this revision the two outcomes are easy to tell apart, because a wrong password
produces a key that decrypts nothing at all rather than plausible rubbish.

### 1.6 Algorithm 2.B — the hash

Let *P* be the prepared password, *S* the salt, and *V* the additional data (the 48 bytes of
`/U` when hashing an owner password, empty otherwise).

```
K = SHA-256(P || S || V)
```

At **`/R 5` that is the whole algorithm** and its first 32 bytes are the result. At **`/R 6`**
the hardening loop follows, and it is the only difference between the two revisions:

```
round = 0
repeat
    K1    = (P || K || V) repeated 64 times
    E     = AES-128-CBC-encrypt(key = K[0..15], iv = K[16..31], K1)   no padding
    m     = (the first 16 bytes of E, as an unsigned big-endian integer) mod 3
    K     = SHA-256(E) if m = 0, SHA-384(E) if m = 1, SHA-512(E) if m = 2
    round = round + 1
until round >= 64 and the last byte of E <= round - 32
result = K[0..31]
```

Two details decide whether an implementation of this is right. The **loop runs at least 64
times** and then continues, one round at a time, until the termination test passes — the test
is never applied before the 64th round. And the modulus is taken over the **first 16 bytes of
E**, not over the whole of it; summing those 16 bytes and taking that sum modulo 3 gives the
same answer, because 256 is congruent to 1 modulo 3.

An implementation that omits the loop computes `/R 5`'s hash. Used against an `/R 6` document
it authenticates nothing, and the document is indistinguishable from one whose password is
wrong. `/app/TEST_INTERFACE.md` §4.1 expects exactly that outcome.

### 1.7 `/Perms` — the permissions, encrypted, and cross-checked

`/Perms` is 16 bytes: one AES-256 block encrypted under the file encryption key in **ECB mode,
with no initialization vector and no padding**. Decrypted, it holds:

```
bytes  0..3    /P, as a 32-bit integer, low-order byte first
bytes  4..7    FF FF FF FF
byte      8    'T' when /EncryptMetadata is true, 'F' when it is false
bytes  9..11   'a', 'd', 'b'
bytes 12..15   unspecified; a reader shall not require any particular value
```

A reader that has recovered the file key **shall decrypt `/Perms` and compare it** with the
`/P` and `/EncryptMetadata` entries of the same dictionary, and shall report a disagreement.
This is the one place in the whole format where the encryption dictionary is checked against
itself, and it is worth stating why it exists: `/P` and `/EncryptMetadata` are plaintext
integers and booleans that anyone may edit, while `/Perms` can only be written by something
that holds the key. A mismatch means the plaintext copy has been altered.

Two failures are distinct and are reported differently. A `/Perms` that decrypts but disagrees
— including the case where only the `/EncryptMetadata` byte disagrees — is a **warning**: the
document is otherwise sound and its contents are still recovered. A `/V 5` dictionary that is
missing `/Perms` or `/UE` altogether is an **error**: there is nothing to check and nothing to
unwrap. `/app/TEST_INTERFACE.md` §2 fixes the status each produces.

### 1.8 What PDF 2.0 deprecates in encryption

The RC4 algorithms are removed from the specification, and with them `/V 1`, `/V 2` and `/R 2`
and `/R 3`; AES-128 as `/V 4` with `/CFM /AESV2` and `/R 4` is deprecated; `/R 5` is
deprecated. **None of that changes what a reader does.** Every one of those documents shall
still be opened, decrypted and reported exactly as `/app/SPEC/pdf16-17.md` requires, whether
its header says 1.7 or 2.0, and a reader shall not refuse, warn about, or annotate a file
merely because the cipher it uses is no longer preferred. Deprecation constrains what a
*writer* should choose; this milestone writes no encrypted file.

### 1.9 Public-key security handlers

`/Filter /Adobe.PubSec` and the entries ISO 32000-1 tables 23, 24 and 27 define shall be read
and reported as written, and such a document shall not be mistaken for an unencrypted one. No
case requires decrypting one. This milestone implements the standard handler only, as
the previous one did.

## 2. Text strings and UTF-8 — 7.9.2 as 2.0 extends it

### 2.1 The three encodings

ISO 32000-1 7.9.2.2 defines a **text string** as PDFDocEncoded, or UTF-16BE introduced by the
two bytes `FE FF`. PDF 2.0 adds a third form: **UTF-8, introduced by the three bytes
`EF BB BF`**. A reader shall therefore decide the encoding of a text string by inspecting its
leading bytes, in this order:

1. it begins `FE FF` — the rest is UTF-16BE;
2. it begins `EF BB BF` — the rest is UTF-8;
3. otherwise — the bytes are PDFDocEncoded.

The introducing bytes are **not part of the value**, and a string that begins with neither
prefix is PDFDocEncoded even if its bytes would also be valid UTF-8. This matters because the
prefix, not the content, is the signal: a UTF-8 string without it is a PDFDocEncoded string
that will decode to something else, and that is the correct reading of it.

The encoding is a property of the string, not of the file: the header version does not gate
it, several encodings may appear in one document, and two strings in the same dictionary may
differ.

### 2.2 Where text strings occur

Everything ISO 32000-1 declares to be a text string: the document information dictionary's
`/Title`, `/Author`, `/Subject`, `/Keywords`, `/Creator` and `/Producer`; an outline item's
`/Title`; an annotation's `/Contents`, `/T`, `/RC` and `/Subj`; a form field's `/V`, `/TU` and
`/TM`; a file specification's `/UF` and `/Desc`; and a collection's strings. Whether the string
was written between `(` and `)` or between `<` and `>` makes no difference: the escape
processing and the hexadecimal decoding happen first, and the encoding is decided on the bytes
that result.

### 2.3 What is not a text string

The operand of a text-showing operator is **not** a text string. It is a string whose bytes
are character codes, interpreted through the font's encoding and the CMap that font names, and
2.0's UTF-8 rule does not reach it. A reader that applies the text-string rule to the operand
of `Tj` or `TJ` will decode a page's content wrongly, and the failure will look like a broken
font rather than a broken string.

Names are not text strings either; a name's `#xx` escapes are unrelated to any of this. A byte
string — `/Contents` of a signature dictionary, an `/ID` value, the hashes in an encryption
dictionary — is not a text string and shall never be decoded as one.

## 3. Associated files — `/AF` and `/AFRelationship`

### 3.1 The association

PDF 2.0 lets an embedded file be **associated with a particular object** rather than only with
the document. Any of these may carry an `/AF` entry whose value is an array of file
specification dictionaries: the catalogue, a page, a form XObject, an image XObject, an
annotation, a structure element, or a document part. The array says that those files belong
with that object; the order is not significant.

An `/AF` entry does not embed anything by itself — each element is a file specification, and
the data lives in the embedded file stream that specification names.

### 3.2 The file specification — 7.11.3, table 44

Unchanged from ISO 32000-1 except for the entry below: `/Type /Filespec`, `/F` and `/UF` for
the name (`/UF` being a text string, so 2.0's UTF-8 rule reaches it), `/EF` holding the
embedded file stream under `/F` and `/UF`, `/Desc` for a description, and `/RF` for related
files (7.11.4.2). The embedded file stream and its `/Params` subdictionary — `/Size`,
`/CreationDate`, `/ModDate`, `/CheckSum` — are as 7.11.4 defines them.

* `/AFRelationship` — a name stating what the associated file **is to** the object that names
  it: `/Source`, `/Data`, `/Alternative`, `/Supplement`, `/EncryptedPayload`, `/FormData`,
  `/Schema` or `/Unspecified`. It belongs in the file specification, not in the array. A
  specification reached through `/AF` shall carry it; one reached only through the name tree
  need not. A name outside that list shall be read and reported as written.

### 3.3 The relationship to the name tree

The catalogue's `/Names /EmbeddedFiles` tree (7.11.4.2) is **unchanged and still the document's
list of embedded files**. `/AF` is an additional statement about association, not a replacement
for that list: a file may appear in both, in the tree alone, or in an `/AF` array alone, and
the three are different documents with different meanings. A reader shall not treat an `/AF`
array as an entry in the name tree, nor the reverse.

A file specification that `/AF` names and that has no `/EF` entry, and an `/AF` array whose
elements do not resolve, are malformed. Neither shall prevent the document from being read.

## 4. Output intents — 14.11.5 as 2.0 extends it

### 4.1 Page-level intents

ISO 32000-1 14.11.5 puts `/OutputIntents` in the document catalogue only. **PDF 2.0 allows the
same array on a page object**, describing the output condition for that page alone. Where a
page carries one, it applies to that page in place of the document-level array; where it does
not, the document-level array applies. A document may have both, one, or neither, and none of
the three is an error.

### 4.2 The output intent dictionary

* `/Type` — optional, `/OutputIntent` if present.
* `/S` — required, a name giving the intent's subtype, for example `/GTS_PDFX`.
* `/OutputCondition`, `/OutputConditionIdentifier`, `/RegistryName`, `/Info` — the human- and
  machine-readable identification of the output condition, as 14.11.5 defines them.
* `/DestOutputProfile` — a stream holding the destination colour profile, embedded in the file.
* `/DestOutputProfileRef` — new in 2.0: a dictionary **referring** to a profile that is not
  embedded, through a file specification under `/CheckSum`, `/ColorantTable` and `/ProfileCS`
  entries. It is a reference, not a substitute: a reader shall not treat a referenced profile
  as though it were present.
* `/MixingHints` — new in 2.0: a dictionary of mixing hints, whose `/Solidities`,
  `/PrintingOrder` and `/DotGain` entries shall be read and reported.
* `/SpectralData` — new in 2.0: a dictionary of spectral characterisation data, read and
  reported.

All of these shall be read and reported as written. **Nothing here is validated**: an intent
with no `/DestOutputProfile` at all, or one whose profile stream is not a profile, is a
document this milestone reads and reports rather than one it rejects. Deciding whether an
output intent is fit for a purpose is not a reader's job.

## 5. Deprecated and removed constructs

PDF 2.0 deprecates or removes a number of constructs that earlier versions define. The
complete rule for all of them, at this milestone, is one sentence: **they shall be read and
reported exactly as written, and their presence shall not change any other answer.**

* **XFA** — the interactive form dictionary's `/XFA` entry, in both of its forms (an array of
  alternating strings and streams, and a single stream). It shall be read and reported, never
  interpreted; the field dictionaries remain the definition of the form, exactly as
  `/app/SPEC/pdf16-17.md` §4.2 already requires. A document carrying one is reported as having
  an interactive form of the XFA kind.
* **`/Movie` and `/Sound` annotations**, and the movie and sound actions that go with them.
  Read and reported as the annotations they are.
* **The catalogue's `/Perms` dictionary with `/UR3`** — the usage-rights mechanism. Note that
  this `/Perms` has nothing whatever to do with the encryption dictionary's `/Perms` of §1.7
  beyond the spelling. It shall be read and reported; the signature dictionaries it holds are
  not in an interactive form's signature field, so they are not signature fields and shall not
  be reported as though they were.
* **The RC4 ciphers and the earlier encryption revisions** — §1.8.
* **`/Lang` as the only carrier of language, deprecated annotation flags, and the other
  entries 2.0 marks obsolete** — read and reported.

A reader shall not mark a document, warn about it, or treat it differently because it contains
a deprecated construct at a 2.0 header. Two files whose bodies are identical and whose headers
say 1.7 and 2.0 shall produce the same report from every utility, save for the version itself.

## 6. Version declaration — 7.5.2 and 7.12

### 6.1 The header

The file begins `%PDF-` followed by the version, and at this milestone that may be **2.0**. A
header version a reader does not recognise — a later one, or a malformed one — shall not
prevent the file from being read.

### 6.2 The catalogue's `/Version`

ISO 32000-1 7.5.2 says the catalogue's `/Version` entry, when present, shall be used instead
of the header's. **PDF 2.0 qualifies that**: the catalogue's value is used only when it is
**later** than the header's, so that an incremental update may raise a document's version but
not lower it. The 2.0 rule is the one to implement:

```
header 1.7, catalogue 2.0   -> the document's version is 2.0
header 1.4, catalogue 2.0   -> 2.0
header 2.0, catalogue 1.7   -> 2.0   (the catalogue is ignored; it is earlier)
header 2.0, no catalogue entry -> 2.0
```

`/Version` is a **name**, not a number or a string: `/2.0`, not `(2.0)`.

### 6.3 The extensions dictionary — 7.12, tables 34 and 35

The catalogue's `/Extensions` entry names the developer extensions a document uses. Each entry
is keyed by a registered developer prefix — `/ADBE` is the one in common use — and holds:

* `/BaseVersion` — a name, the version of the base specification the extension is relative to,
  written as `/1.7` or `/2.0`;
* `/ExtensionLevel` — an integer, the extension level assigned by that developer;
* `/URL`, `/ExtensionRevision` — optional, read and reported.

An extension this implementation knows nothing about is not an error: the dictionary is read
and reported, and the document is read as though the extension were absent. The extension
level is **part of what a reader reports about the version**, alongside the version itself.

## 7. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `pdfinfo` — the fields of the previous milestones. `Encrypted` now names AES-256 as the
  algorithm of an `/AESV3` document, and carries the permissions `/P` gives, as before.
  `PDF version` carries §6's answer: the header's version, or the catalogue's where §6.2 says
  the catalogue wins.
* `pdftotext`, `pdffonts` — the text and the fonts of a document decrypted at `/V 5` exactly as
  at `/V 4`: decryption is transparent to everything above it, and a page of an AES-256
  document extracts as ordinary text.
* `pdfimages`, `pdfsig` — unchanged from the previous milestone, over documents that may now
  be encrypted at `/V 5`.
* `qpdf --show-encryption` — `/R`, `/P`, the permissions they imply, and, because `/V` is 4 or
  greater, the method used for streams, strings and the file, which for `/AESV3` is named
  `AESv3`. `--show-encryption-key` adds the file encryption key §1.5 recovered, as hexadecimal.
* `qpdf --check` — the same block, the document's version, the extension level where §6.3
  gives one, and the `/Perms` disagreement of §1.7 where there is one.
* `qpdf --is-encrypted` — whether the document is encrypted, without a password.
* `qpdf --show-object=N` — one object of a decrypted document, the `/V 5` encryption dictionary
  included, with `/U`, `/O`, `/UE`, `/OE` and `/Perms` as the hexadecimal strings they are; and
  any of §§3 to 6's structures, as written.
* `qpdf --decrypt` — the same document with no encryption on it, and the same declared version
  it had before.
