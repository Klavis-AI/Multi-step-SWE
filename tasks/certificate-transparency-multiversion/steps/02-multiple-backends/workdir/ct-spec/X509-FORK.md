# X509-FORK — the forked `asn1/` and `x509/` packages

The product ships its own copies of Go's `encoding/asn1` and `crypto/x509` at
the import paths

    github.com/google/certificate-transparency-go/asn1
    github.com/google/certificate-transparency-go/x509
    github.com/google/certificate-transparency-go/x509/pkix

They are forks: the upstream Go source with deliberate, marked changes. Both
are present at **every** anchor v1.0..v1.3.3.

---

## 1. Why a fork exists at all

A CT log is the wrong place to be strict. Its job is to record what certificate
authorities actually issued, and a large fraction of what they actually issued
does not survive a conforming DER parser. If the log refuses a certificate, the
certificate simply goes unlogged — the defect is hidden rather than fixed,
which is the opposite of what Certificate Transparency is for.

So the fork exists to do two things Go's standard library deliberately will
not:

1. **Parse malformed certificates anyway**, while recording exactly what was
   wrong, so a caller that cares can still find out.
2. **Expose the precertificate machinery** RFC 6962 §3.1 needs — the poison
   extension, and reconstruction of the TBSCertificate that was signed.

Everything else in the fork is upstream Go. The changes in `x509.go` are
bracketed by `// START CT CHANGES` / `// END CT CHANGES` comments at the early
anchors.

A candidate is **not** expected to re-derive the whole of `crypto/x509`. What
the verifier reaches for is the CT-specific surface described below, exercised
against pinned certificate fixtures. Whether the rest of the parse comes from a
wrapper over the standard library or from a genuine fork is not observed,
except where the two disagree — and the points where they disagree are exactly
what §3 and §5 enumerate.

---

## 2. `asn1/` — the laxness

The fork's own package comment states the difference. At **v1.0..v1.0.21** it
is a single behaviour: `PrintableString` contents that are not actually
printable (typically ISO 8859-1 stuffed into a `PrintableString` tag) are
recovered rather than rejected.

Two properties of that behaviour matter, and they pull in opposite directions.
It is **unconditional** — nothing turns it on, there is no tag and no option,
so every caller of the v1.0..v1.0.21 `asn1` gets it. But it is **scoped to the
ANY branch**: the recovery lives at the `interface{}` case of `parseField`,
where the parser is deciding what a universal-class primitive is. A field
declared in Go as a plain `string` takes the other path, which calls
`parsePrintableString` and returns its error, so a non-printable
`PrintableString` in a *typed* field is still a parse failure at these
releases.

At **v1.1.0** both properties invert. The recovery moves inside
`parsePrintableString` itself, which gains a `lax` parameter, so it is now
**conditional** — off unless the `lax` tag (§2.1) reaches that field — and
both call sites pass the flag, so when it is on it applies to typed string
fields as well as to ANY. The same release generalises the mechanism into the
field tag whose three relaxations the package comment names. This is the
change graded at step 8.

### 2.1 The `lax` tag

    Field  T  `asn1:"lax"`

`lax` **applies recursively**: it relaxes the field it is written on and every
field nested inside it. It is also accepted by
`asn1.UnmarshalWithParams(data, val, "lax")` as a whole-document parameter,
which is how the x509 layer uses it (§3.2).

### 2.2 The three relaxations, exactly

| relaxation | strict behaviour | `lax` behaviour |
| --- | --- | --- |
| `PrintableString` with invalid characters | `SyntaxError{"PrintableString contains invalid character"}` | try ISO 8859-1, then T.61, and **failing both it is still an error** — `SyntaxError{"PrintableString contains invalid character, couldn't determine correct String type"}`, with nothing assigned to the destination |
| non-minimally-encoded `INTEGER` | `StructuralError{"integer not minimally-encoded"}` | accepted as-is |
| zero-length `OBJECT IDENTIFIER` | `SyntaxError{"zero length OBJECT IDENTIFIER"}` | yields an empty `ObjectIdentifier`, no error |

A single-byte integer is always accepted; an empty integer is always
`StructuralError{"empty integer"}`, `lax` or not.

Note what relaxation 1 does **not** do. The recovery never falls back to
handing the caller the raw bytes: if neither encoding fits, the parse fails and
the destination keeps its zero value. A NUL byte is the standard way to reach
that branch, being legal in neither ISO 8859-1 nor T.61.

**Which characters are invalid in the first place.** The accepted set the
parser works from is the RFC PrintableString set plus two characters the fork
admits on purpose: `*`, because wildcard subject names in the wild are put in
the wrong string type, and `&`, because a handful of long-lived CA
certificates contain one. In ascending byte order the whole accepted set is

    0x20         space
    0x26         &            — not RFC PrintableString; admitted on purpose
    0x27..0x29   ' ( )
    0x2A         *            — not RFC PrintableString; admitted on purpose
    0x2B..0x2F   + , - . /
    0x30..0x39   0-9
    0x3A :   0x3D =   0x3F ?
    0x41..0x5A   A-Z
    0x61..0x7A   a-z

— 76 bytes. Each of the other 180 is an invalid character, and in strict mode
any one of them fails the parse. Both allowances are **inherited, not part of
`lax`**: they apply in strict mode too, and they are what relaxation 1 is
measured against. Version detail: `&` and the asterisk/ampersand pair arrive at
**v1.0.10**; at **v1.0** and **v1.0.5** the set is the same list without `&`,
so 75 bytes. The set above is the set used when **parsing**; marshalling a
`PrintableString` applies the same test without the `&` allowance.

### 2.3 Diagnostics

The second, independent difference from the standard library: every error
carries the **field name** it occurred in. `StructuralError` and `SyntaxError`
each gain a `Field` alongside `Msg`, and the rendered text names the field. A
verifier may assert on the message substrings above; it does not assert on the
exact rendering of the field path.

### 2.4 `BMPString` (from **v1.1.2**)

v1.1.2 merges Go 1.14's BMPString support into the fork. Two observable
consequences, both at the exported boundary:

* the constant `TagBMPString` (universal tag **30**) exists, where before
  v1.1.2 it did not;
* `Unmarshal` into an `interface{}` or a `RawValue`-free string field decodes
  a universal tag-30 element as UCS-2 **big-endian**, decoded through UTF-16,
  and strips one trailing two-byte NUL terminator if present. An odd-length
  body is an error, `pkcs12: odd-length BMP string` — the message keeps the
  provenance of the code it was lifted from.

Like the other string types, a BMPString is accepted where a `PrintableString`
is expected when parsing a `SEQUENCE OF` into a `[]string`. Before v1.1.2 the
same input is a tag-mismatch `StructuralError`.

---

## 3. `NonFatalErrors`

The fork's central idea, and the thing most likely to be got wrong, because it
inverts the usual Go contract: **a function may return a non-nil result and a
non-nil error at the same time.**

    type NonFatalErrors struct {
        Errors []error
    }
    func (e *NonFatalErrors) AddError(err error)
    func (e NonFatalErrors) Error() string
    func (e *NonFatalErrors) HasError() bool
    func IsFatal(err error) bool           // from v1.0.21

`Error()` renders as `NonFatalErrors: ` followed by each contained error's own
text, each terminated by `"; "`. `HasError()` is true when the slice is
non-empty.

### 3.1 The contract

When parsing hits a defect it can work around, it appends to a
`NonFatalErrors` and keeps going. At the end, if anything was collected, the
parse returns `(result, nfe)` — **both** non-nil. A caller that wants the
lenient behaviour must therefore test the error's type, not its nil-ness:

* error is nil → clean parse.
* error is a `NonFatalErrors` → the `*Certificate` is usable; the errors
  describe what was tolerated.
* any other error type → the parse failed and the result is nil.

From **v1.0.21** the fork exports that test as a package-level predicate, so
callers stop writing the type switch by hand:

    func IsFatal(err error) bool

`IsFatal(nil)` is false; `IsFatal` of a `NonFatalErrors` is false; `IsFatal`
of a structured `*Errors` (§5.2) defers to that value's own fatal/non-fatal
verdict; anything else is fatal, and true. So `if x509.IsFatal(err) { … }` is
this release's spelling of "the parse really failed", and `err != nil` remains
the spelling of "something was tolerated or something failed" — the two are
different questions and the fork's own callers ask the first one. `IsFatal`
is present from v1.0.21 through v1.3.3.

`ctfe`'s chain validation takes exactly this stance: a certificate that parses
with non-fatal errors is **admissible**. A CTFE that rejects such a submission
is wrong, and so is one that returns nil from its parse wrapper when the error
is non-nil.

Things that land in `NonFatalErrors` rather than failing the parse include: an
`iPAddress` SAN whose length is neither 4 nor 16, an unhandled critical
extension in certain positions (`UnhandledCriticalExtension` — the positions
are enumerated for `nameConstraints` below, which is the only extension that
reaches this at v1.0), and, from
v1.0.21, the two extensions added in that release — but **not symmetrically**,
and the asymmetry is graded:

| v1.0.21 extension | defect | fatal? |
| --- | --- | --- |
| `SubjectInfoAccess` (1.3.6.1.5.5.7.1.11) | contents will not `asn1.Unmarshal`, or leave trailing data | **fatal** — `ParseCertificate` returns a nil certificate and the error |
| `SubjectInfoAccess` | present but **empty** (`SEQUENCE` of zero entries) | non-fatal, `x509: empty SubjectInfoAccess extension` |
| `sbgp-ipAddrBlock` (RFC 3779) | contents will not `asn1.Unmarshal`, or leave trailing data | **non-fatal** — the field is left nil and the parse succeeds |
| `sbgp-autonomousSysNum` (RFC 3779) | likewise | **non-fatal**, likewise |

So SIA is parsed strictly and RPKI leniently. The reason is structural rather
than a matter of taste: the SIA branch is inline in `parseCertificate` and
returns on error, while both RFC 3779 branches delegate to helpers
(`parseRPKIAddrBlocks`, `parseRPKIASIdentifiers`) that take the
`*NonFatalErrors` and report into it. A test that submits a certificate with a
corrupt SIA and expects a tolerated parse is asserting the wrong thing.

The wording of each of these diagnostics changes across releases and is not
part of the contract; what is part of the contract is which side of the
fatal/non-fatal line each defect falls on.

#### `nameConstraints`, and what "unhandled" means there

"Unhandled" is decided per *part* of an extension, not per extension, and
`nameConstraints` (OID **2.5.29.30**, RFC 5280 §4.2.1.10) is where the two come
apart. It is never rejected: at no release does a parsable `nameConstraints`
make `ParseCertificate` return a nil certificate. What changes is whether
parsing one contributes to `NonFatalErrors`.

At **v1.0** the parser understands `permittedSubtrees` and has no field at all
for `excludedSubtrees`. So at v1.0, and at v1.0 only:

| what the extension carries | critical? | result at v1.0 |
| --- | --- | --- |
| a non-empty `excludedSubtrees` | **yes** | **non-fatal**: `UnhandledCriticalExtension{2.5.29.30}` is added to the `NonFatalErrors` — the excluded half is genuinely unhandled — and the parse continues |
| a non-empty `excludedSubtrees` | no | nothing is recorded |
| a `permittedSubtrees` entry whose `base` is the **empty** name | **yes** | **non-fatal**, same error value; that one entry is skipped and the rest are kept |
| a `permittedSubtrees` entry whose `base` is the empty name | no | that entry is skipped silently |
| well-formed `permittedSubtrees` | either | nothing is recorded |

In every one of those rows the returned `*Certificate` is complete and
`PermittedDNSDomains` holds the permitted subtrees exactly as it would have
without the excluded half: the caller of a critical
`excludedSubtrees`-carrying certificate gets `(cert, nfe)` with **both**
non-nil and the permitted names intact, which is §3.1's contract applied here.
A parser that populates `PermittedDNSDomains` and then returns a nil error
because it considers the extension handled is wrong at v1.0.

From **v1.0.5** the fork parses the excluded half too, into
`Certificate.ExcludedDNSDomains`, and records `PermittedDNSDomainsCritical`.
The first row above therefore stops producing a non-fatal error at v1.0.5 and
stays clean through v1.3.3; the empty-`base` rows keep their behaviour at every
release, now on both halves.

#### The SAN relaxation at v1.0.17

What is *fatal* about a SAN extension narrows at **v1.0.17**, and this one is
graded:

| release | `dnsName` that is not a parsable domain | `rfc822Name` that is not a parsable RFC 2821 mailbox |
| --- | --- | --- |
| v1.0 .. v1.0.13 | **fatal** — `ParseCertificate` returns a nil certificate and an error | **fatal**, likewise |
| **v1.0.17** onward | **tolerated** — the parse succeeds and the raw string is stored verbatim in `DNSNames` | **tolerated** — stored verbatim in `EmailAddresses` |

So from v1.0.17 `Certificate.DNSNames` and `Certificate.EmailAddresses` may
hold strings that are *not* valid domain names or mailboxes: they are exactly
the bytes the extension carried, uninterpreted and unmodified, in extension
order, and a well-formed SAN is stored exactly as before. This is the fork's
purpose applied to names — a log must be able to observe a certificate that
should never have been issued.

Validation does not disappear; it **moves** to the point where a name is
actually used. From v1.0.17 the name-constraint walk inside `Verify` parses
the name it is about to compare and fails the chain if it cannot
(`CertificateInvalidError`-level, not non-fatal). A certificate with a
malformed `dnsName` therefore parses, but cannot be verified against an issuer
carrying name constraints.

### 3.2 Which functions do this, and from when

| release | behaviour |
| --- | --- |
| **v1.0** | `parseCertificate` collects non-fatal errors and returns them, so `ParseCertificate`, `ParseCertificates` and `ParseTBSCertificate` surface them **only** as the plain error they already return — a caller that ignores a `NonFatalErrors` gets a usable certificate. |
| **v1.1.0** | The whole `ParseCertificate` family is restructured: each of `ParseCertificate`, `ParseCertificates` and `ParseTBSCertificate` **accumulates** its own `NonFatalErrors`, merges in any that `parseCertificate` produced, and returns `(result, nfe)`. Each also **retries with `lax`**: if the strict `asn1.Unmarshal` fails, it re-runs as `asn1.UnmarshalWithParams(..., "lax")`, and if *that* succeeds the original strict error is recorded as non-fatal. Their doc comments say so explicitly. |

The v1.1.0 behaviour is the one the step-8 checks observe: a certificate that
the strict parser rejects and the lax parser accepts must come back as a usable
`*Certificate` together with a `NonFatalErrors` that contains the strict
error's text.

Two details separate the plural function from the other two, and both hold
unchanged from v1.1.0 through v1.3.3.

* `ParseCertificates` loops over a concatenation, so the `NonFatalErrors` it
  returns is the union over every certificate in the input. A well-formed
  certificate in front of a defective one does not shield it: both come back
  in the slice, and the single error carries the defect.
* Its lax retry is wired to the address of an already-pointer local — a
  `**certificate`, not a `*certificate` — so the retry never succeeds. The
  consequence is exact, and it is what the release does: everything
  `parseCertificate` recovers *after* the outer `asn1.Unmarshal` has already
  succeeded — a non-minimally-encoded RSA exponent, a zero-length OBJECT
  IDENTIFIER inside an extension, a `PrintableString` holding bytes outside
  the set, a negative RSA modulus — `ParseCertificates` recovers too, because
  that work is shared. A defect in the *outer* certificate structure, where
  the strict `asn1.Unmarshal` itself is what failed, is recovered only by
  `ParseCertificate`; `ParseCertificates` returns `nil` and a fatal
  `asn1.SyntaxError` for the same bytes. Both halves of that contrast are
  graded.

Trailing data after the certificate is **always** fatal:
`asn1.SyntaxError{Msg: "trailing data"}`, with no lax fallback.

---

## 4. Precertificates

RFC 6962 §3.1 defines a precertificate as a certificate-shaped object carrying
a **critical poison extension** which makes it unusable as a real certificate.
The log removes the poison, and the resulting TBSCertificate is what the SCT
signature covers.

### 4.1 The poison extension

    OIDExtensionCTPoison = 1.3.6.1.4.1.11129.2.4.3

Critical, with a value of ASN.1 `NULL` (`05 00`). Also relevant:

    OIDExtensionCTSCT (the SCT list extension)     1.3.6.1.4.1.11129.2.4.2
    ExtKeyUsageCertificateTransparency             1.3.6.1.4.1.11129.2.4.4

### 4.2 `RemoveCTPoison`, `RemoveSCTList` and `BuildPrecertTBS`

    func RemoveCTPoison(tbsData []byte) ([]byte, error)
    func RemoveSCTList(tbsData []byte) ([]byte, error)
    func BuildPrecertTBS(tbsData []byte, preIssuer *Certificate) ([]byte, error)

Both take **DER-encoded TBSCertificate** bytes and return DER-encoded
TBSCertificate bytes. `RemoveCTPoison(x)` is `BuildPrecertTBS(x, nil)`.

Required behaviour, at every anchor:

1. Unmarshal the input as a TBSCertificate. A parse failure is
   `failed to parse TBSCertificate: <err>`; trailing bytes are
   `trailing data (<n> bytes) after TBSCertificate`.
2. Find the poison extension. **Exactly one** must be present:
   zero is `no CT poison extension present`, two or more is
   `multiple CT poison extensions present`.
3. Remove it, **preserving the order of every other extension**. This is not
   cosmetic: the output is re-serialised and hashed, so any reordering changes
   the leaf and invalidates the SCT.
4. Re-serialise. The cached `Raw` of the parsed structure must be discarded so
   the encoder rebuilds it rather than echoing the input.

When `preIssuer` is non-nil it is the special intermediate that signed the
precertificate, and the issuance information is rewritten to name the *next*
issuer up the chain:

* The precert's `Issuer` is replaced by the **raw** issuer bytes of the
  intermediate (`RawIssuer`), not a re-encoding of the parsed name — so that a
  `UTF8String`/`PrintableString` difference cannot creep in.
* The precert's Authority Key Identifier extension is replaced by the
  intermediate's, read out of the **parsed** `*Certificate` it was handed: the
  entry of `preIssuer.Extensions` whose `Id` is `2.5.29.35`, whose `Value` is
  copied verbatim into the rewritten TBS, and the `preIssuer.AuthorityKeyId`
  field that entry produced. `BuildPrecertTBS` does **not** re-parse
  `preIssuer.Raw` and does not consult it at all; the parsed struct is the
  authority. A pre-issuer whose parsed `Extensions` and `AuthorityKeyId` show
  no AKI is therefore refused with `issuer has no auth-key-id extension` even
  if its `Raw` bytes still carry the extension. This holds at every anchor.
* The intermediate must carry the CT extended key usage. If not:
  `issuer does not have CertificateTransparency extended key usage`.

`RemoveSCTList` is the same removal applied to the **SCT-list** extension
(`OIDExtensionCTSCT`, §4.1) instead of the poison one: same DER-in/DER-out
contract, same order preservation, same discarded `Raw`, and the same
exactly-one rule. Its failures are therefore the same five: input that is not
a TBSCertificate, input with trailing bytes after one, a TBS carrying **no**
SCT-list extension, a TBS carrying **more than one**, and — since a poisoned
precertificate carries no SCT list — a precertificate TBS. It is what a caller
uses to get back from an issued certificate to the TBS the SCT was computed
over.

### 4.3 The issuer key hash

The `PreCert` structure hashed into the leaf (TLS-CODEC.md §5.2) pairs the
rebuilt TBS with `issuer_key_hash`: the SHA-256 of the DER
`SubjectPublicKeyInfo` **of the issuer that signed the precertificate** — which,
when a pre-issuer was used, is the next issuer up, matching the rewritten
`Issuer` field above.

### 4.4 Recognising a precertificate

    func (c *Certificate) IsPrecertificate() bool

A single `bool`, not a `(bool, error)` pair: a certificate is a
precertificate exactly when its parsed extensions include the poison OID of
§4.1. It is **nil-safe** — a nil receiver is `false`, not a panic — and it is
`false` for a plain certificate, for a CA certificate, and for a certificate
carrying an embedded SCT list, which is the poison extension's opposite
number. Do not confuse it with `ctfe.IsPrecertificate`, a different function
in a different package with a `(bool, error)` result (CLIENT-API.md §8).

### 4.5 The SCT-list carrier types — from v1.0.10

Everything in this subsection arrives at **v1.0.10** and is unchanged after
it. At v1.0 and v1.0.5 the fork has neither carrier type and neither
`Certificate` field: the embedded SCT-list extension is left in
`Certificate.Extensions` as raw DER for the caller to decode.

The `x509util` helpers that walk a decoded list are **later still** and must
not be assumed to arrive with the carrier types: `ParseSCTsFromSCTList`,
`ParseSCTsFromCertificate` and `ExtractSCT` all first appear at **v1.0.13**
(CLIENT-API.md §5.2's arrival table). At v1.0.10 the types below exist and
those functions do not.

The embedded SCT list of RFC 6962 §3.3 is carried by two exported structs,
each with a single exported field:

    type SerializedSCT struct {
        Val []byte
    }

    type SignedCertificateTimestampList struct {
        SCTList []SerializedSCT
    }

`SerializedSCT.Val` holds the TLS encoding of **one whole SCT**
(TLS-CODEC.md §5.5) — not a parsed structure and not the extension's DER
wrapper. Turning one into a `ct.SignedCertificateTimestamp` is a TLS
unmarshal of those bytes, and from v1.0.13 — not before — `x509util.ExtractSCT`
does it for you and `x509util.ParseSCTsFromSCTList` walks `SCTList` in order
applying it to every entry (CLIENT-API.md §5.2). Order is preserved either
way: the *n*th result comes from the *n*th entry.

`SignedCertificateTimestampList` is an ordinary Go struct, so its zero value
is a list of no SCTs and it is perfectly constructible; the `minlen` bound
that RFC 6962 places on the wire form is enforced by the TLS codec at
marshalling time, not by the type. Walking an empty list therefore yields no
SCTs and **no error**.

From v1.0.10 a parsed `*Certificate` exposes the same list twice, in two new
exported fields: `RawSCT []byte` is the extension content with its outer
DER `OCTET STRING` stripped but otherwise undecoded, and
`SCTList SignedCertificateTimestampList` is that content TLS-decoded — so the
entries of a certificate's embedded list are reached as
`cert.SCTList.SCTList`. Both are populated by `ParseCertificate` whenever the
`OIDExtensionCTSCT` extension is present and decodes; both are also read by
`CreateCertificate`, which will synthesise the extension from whichever of the
two a template supplies.

---

## 5. Other CT-specific surface

### 5.1 `ParseTBSCertificate`

    func ParseTBSCertificate(asn1Data []byte) (*Certificate, error)

Parses bare TBSCertificate DER — no outer signature wrapper — into the same
`*Certificate` type. Standard Go has no such entry point. It is what lets a
rebuilt precert TBS be inspected, and it is CT-changes code from v1.0.

### 5.2 Structured errors (from **v1.0.5**)

`x509/errors.go` adds an `ErrorID` enumeration and
`func NewError(id ErrorID, args ...interface{}) Error`, giving each
certificate defect a stable identity rather than only a string. `x509/names.go`
and `x509/revoked.go` arrive in the same release. `x509/revoked.go` is where
the CRL reason code lives, and its name is
**`type RevocationReasonCode asn1.Enumerated`** — there is no
`x509.RevocationReason`. The type is present from v1.0.5 onwards and is
unchanged through v1.3.3; what arrives later is the renderer for it,
`x509util.RevocationReasonToString`, at v1.0.10 (CLIENT-API.md §5.2).

### 5.3 SIA and RPKI (from **v1.0.21**)

`x509/rpki.go` and the Subject Information Access handling are added at
v1.0.21 and are part of step 7.

| OID | constant | populates |
| --- | --- | --- |
| 1.3.6.1.5.5.7.1.11 | `OIDExtensionSubjectInfoAccess` | — |
| 1.3.6.1.5.5.7.48.3 | `OIDSubjectInfoAccessTimestamp` | `Certificate.SubjectTimestamps []string` |
| 1.3.6.1.5.5.7.48.5 | `OIDSubjectInfoAccessCARepo` | `Certificate.SubjectCARepositories` |

RFC 3779 address and AS-number blocks arrive in the same release, carried by
two more extensions and parsed into `x509/rpki.go`'s types. The extensions:

| OID | constant | populates |
| --- | --- | --- |
| 1.3.6.1.5.5.7.1.7 | `OIDExtensionIPPrefixList` | `Certificate.RPKIAddressRanges []*IPAddressFamilyBlocks` |
| 1.3.6.1.5.5.7.1.8 | `OIDExtensionASList` | `Certificate.RPKIASNumbers` and `Certificate.RPKIRoutingDomainIDs`, both `*ASIdentifiers` |

The `asNum` arm of the AS-identifier extension populates `RPKIASNumbers` and
the `rdi` arm populates `RPKIRoutingDomainIDs`; either may be absent, in which
case its field stays nil. The types, in full, because a caller reads their
fields directly:

    type IPAddressPrefix asn1.BitString   // so: fields Bytes []byte, BitLength int
    type IPAddressRange struct {
        Min IPAddressPrefix
        Max IPAddressPrefix
    }
    type IPAddressFamilyBlocks struct {
        AFI               uint16          // 1 = IPv4, 2 = IPv6
        SAFI              byte
        InheritFromIssuer bool
        AddressPrefixes   []IPAddressPrefix
        AddressRanges     []IPAddressRange
    }
    type ASIDRange struct {
        Min int
        Max int
    }
    type ASIdentifiers struct {
        InheritFromIssuer bool
        ASIDs             []int
        ASIDRanges        []ASIDRange
    }

`IPAddressPrefix` is a **named type over `asn1.BitString`**, not a struct of
its own, which is where its two field names come from: `Bytes` holds the
prefix bytes and `BitLength` the prefix length in bits, so a `/24` is three
bytes with `BitLength` 24. The constants
`IPv4AddressFamilyIndicator = uint16(1)` and
`IPv6AddressFamilyIndicator = uint16(2)` are exported alongside. When
`InheritFromIssuer` is set the two slices beside it are empty: the resources
are the issuer's. All of these names are stable from v1.0.21 to v1.3.3.

The two halves of this release's extension work do **not** treat a malformed
extension the same way, and §3.1 tabulates it. An *empty* SIA extension is the
non-fatal `x509: empty SubjectInfoAccess extension`, so a certificate carrying
one still parses. SIA contents that will not unmarshal, or that leave trailing
data, are **fatal**: the branch is inline in `parseCertificate` and returns
`nil, err`, the trailing-data case with `x509: trailing data after X.509
subject information`. The RFC 3779 extensions are the lenient pair — both
`sbgp-ipAddrBlock` and `sbgp-autonomousSysNum` route every parse failure
through `NonFatalErrors` and leave the corresponding field nil, so the
certificate parses either way. Do not generalise from one to the other.

### 5.4 Mismatching signature-algorithm identifiers (**v1.3.3** only)

An X.509 certificate carries its signature algorithm **twice**: once inside the
signed `tbsCertificate` and once in the outer, unsigned `Certificate`. RFC 5280
requires them to match; Go does not enforce it, and the outer copy is not
covered by the signature, so its OID and parameter bytes are freely malleable.
That is a duplicate-submission amplifier for a log: an attacker can mint an
unbounded number of byte-distinct but semantically identical certificates that
each pass the backend's leaf-uniqueness check.

From v1.3.3, when parsing a **full certificate** (not a bare TBS), the two
`AlgorithmIdentifier` structures are compared **byte for byte** and a mismatch
is fatal:

    x509: mismatching signature algorithm identifiers

`ParseTBSCertificate` is exempt, because there is only one identifier to
compare. This is a step-13 behaviour and a certificate that parsed at v1.2.0
may legitimately fail to parse at v1.3.3.

**Which of the two identifiers the certificate reports, at every release.**
`Certificate` exposes exactly one `SignatureAlgorithm`, and it is the **inner**
one: `parseCertificate` sets the field from the TBS copy, from
`SignatureAlgorithmFromAI(in.TBSCertificate.SignatureAlgorithm)`, which is
also what the standard library does. Nothing on the returned `Certificate`
carries the outer identifier — at v1.3.3 it is read only to be compared, as
above. So through **v1.2.0**, where a mismatch is not fatal, a certificate
whose two identifiers disagree parses with **no error at all** — the
disagreement is not recorded in `NonFatalErrors` either; it is a policy
matter, not a parse defect — and its `SignatureAlgorithm` is the **TBS**
algorithm. A certificate whose outer identifier is `ecdsa-with-SHA384` and
whose TBS identifier is `ecdsa-with-SHA256` therefore reports
**`ECDSA-SHA256`**, never `ECDSA-SHA384`. The rule does not change at v1.3.3;
there, such a certificate is simply fatal and never gets that far.
`CheckSignatureFrom` is consistent with it by construction: it verifies with
the field's value over `RawTBSCertificate`, the bytes the inner identifier is
the one that signed.

### 5.5 The Go 1.13 / 1.14 merge (**v1.1.2**)

The fork tracks upstream Go by periodic merges, and v1.1.2's header records
one: *"Merged upstream Go 1.13 and Go 1.14 changes"*. Three of the merged
behaviours are visible through the exported boundary and are part of the
step-9 milestone.

**Ed25519.** The `PureEd25519` signature algorithm and the `Ed25519`
public-key algorithm arrive, with `id-Ed25519` = **1.3.101.112**. A
`SubjectPublicKeyInfo` for an Ed25519 key marshals and parses; certificates
are created and their signatures checked with **no pre-hash** — the message
itself is signed. RFC 8410 §3 requires the algorithm parameters to be
**absent**, and the fork enforces it: parameters present make the key, and a
PKCS#8 private key carrying them, invalid. Ed25519 is deterministic, so every
byte of a key, an SPKI and a signature is a fixed function of the seed.

Both constants render as **`Ed25519`**: `SignatureAlgorithm.String()` on
`PureEd25519` and `PublicKeyAlgorithm.String()` on `Ed25519` each return the
RFC 8410 name rather than the Go constant's spelling, so `PureEd25519` does
*not* print as `PureEd25519`. The mapping in the other direction is exported
in the fork and unexported in the standard library:
`SignatureAlgorithmFromAI(pkix.AlgorithmIdentifier) SignatureAlgorithm` turns
a parsed algorithm identifier back into the constant, and section 1's mirror
rule therefore does not supply it.

**DSA digest truncation.** FIPS 186-3 §4.6 says a digest longer than the
subgroup order `q` is truncated to the leftmost `len(q)` bits before
verification. Go's `crypto/dsa` does not do this, so the fork does it itself.
The case that distinguishes the two is a 1024-bit DSA key — `q` is 160 bits —
with a SHA-256 signature: correct truncation verifies it, and passing the full
digest does not.

**`Certificate.Equal` is nil-safe.** Two nil receivers are equal, a nil and a
non-nil are not, and neither combination panics.

One upstream change in that range is deliberately **not** merged, and the
product says so in its own release notes: the fork keeps the pre-Go-1.14 rule
so that certificates issued under a malformed root still parse and can
therefore still be logged. It is the argument of §1 applied to a single
upstream commit, and it is the reason "merged upstream Go" is never quite the
same as "is upstream Go".

### 5.6 `VerifyOptions` verification switches (from **v1.0.17**)

Standard Go's `x509.VerifyOptions` offers no way to turn a check off; a log
needs exactly that, because it must record certificates a browser would
reject. Up to **v1.0.13** the fork had one switch, `DisableTimeChecks`, and
callers that needed more edited the parsed certificates by hand before calling
`Verify`. At **v1.0.17** the switch set is completed and the hand-editing in
`trillian/ctfe` is deleted (HTTP-API.md §5.1):

    // v1.0.17 onwards
    type VerifyOptions struct {
        DNSName       string
        Intermediates *CertPool
        Roots         *CertPool
        CurrentTime   time.Time
        // Options to disable various verification checks.
        DisableTimeChecks              bool
        DisableCriticalExtensionChecks bool
        DisableNameChecks              bool
        DisableEKUChecks               bool
        DisablePathLenChecks           bool
        DisableNameConstraintChecks    bool
        KeyUsages                 []ExtKeyUsage
        MaxConstraintComparisions int
    }

| field | from | when false, what is enforced |
| --- | --- | --- |
| `DisableTimeChecks` | v1.0 | every certificate in the path is valid at `CurrentTime` (or at the wall clock if that is zero) |
| `DisableCriticalExtensionChecks` | **v1.0.17** | a certificate with a non-empty `UnhandledCriticalExtensions` is rejected — which is every precertificate, because the poison extension is critical and unhandled |
| `DisableNameChecks` | **v1.0.17** | each certificate's `RawIssuer` equals its issuer's `RawSubject` |
| `DisableEKUChecks` | **v1.0.17** | the leaf-EKU and CA-nesting tests below run |
| `DisablePathLenChecks` | **v1.0.17** | `BasicConstraints` path-length limits bound the number of intermediates |
| `DisableNameConstraintChecks` | **v1.0.17** | an issuer's name constraints are applied to the leaf's SANs |

Each switch is independent, defaults to `false`, and suppresses only its own
check: a chain admitted with `DisableEKUChecks` is still time-checked, still
name-chained and still signature-checked.

#### Leaf EKU matching, also v1.0.17

In the same release `ekuPermittedBy` gains a context argument, and the
exceptions it grants stop applying to the leaf. With `DisableEKUChecks` false
and `KeyUsages` non-empty:

* **against the leaf's own EKU list**, an EKU is permitted only by an exact
  match or by `ExtKeyUsageAny` (plus the `ServerAuth` OID aliasing the fork
  already had). Requesting `ClientAuth` or `OCSPSigning` from a leaf that
  lists only `ServerAuth` is now refused — before v1.0.17 both were allowed by
  the CA-oriented exceptions;
* **against a CA's EKU list** the historical exceptions remain: `ServerAuth`
  in a CA permits `ClientAuth` below it, any CA may issue an OCSP responder,
  and a code-signing CA covers Microsoft's commercial and kernel-mode EKUs;
* a leaf carrying **no** EKU extension at all is accepted for any request —
  "no EKUs listed" means unconstrained, not "nothing permitted". (This is the
  opposite of the CTFE's own leaf test at v1.1.2, HTTP-API.md §5.4, which
  refuses an EKU-free leaf. The two rules live in different layers.)

A refusal is `CertificateInvalidError` with reason `IncompatibleUsage`.

---

### 5.7 An insecure curve, on purpose (from **v1.0.21**)

`x509/curves.go` arrives at v1.0.21 and adds one elliptic curve the standard
library deliberately does not carry: **secp192r1**, also called NIST P-192,
too short to be safe and observed in certificates in the wild. A log has to be
able to *observe* such a certificate, which is the fork's whole purpose, so
the curve is implemented as an `elliptic.Curve` with `Params().Name` `"P-192"`
and `BitSize` 192, and the named-curve table gains an arc:

| curve | OID |
| --- | --- |
| **secp192r1 / P-192** | `OIDNamedCurveP192` = 1.2.840.10045.3.1.1 |
| secp224r1 / P-224 | `OIDNamedCurveP224` = 1.3.132.0.33 |
| secp256r1 / P-256 | `OIDNamedCurveP256` = 1.2.840.10045.3.1.7 |
| secp384r1 / P-384 | `OIDNamedCurveP384` = 1.3.132.0.34 |
| secp521r1 / P-521 | `OIDNamedCurveP521` = 1.3.132.0.35 |

`OIDFromNamedCurve(curve elliptic.Curve) (asn1.ObjectIdentifier, bool)` maps
back, and from v1.0.21 it knows P-192 as well as the other four.

The change is deliberately **asymmetric**, and both halves are part of the
release:

* Inside a **certificate**, a P-192 subject public key is now parsed. The
  curve's insecurity is demoted to a non-fatal diagnostic (§3), so
  `ParseCertificate` returns a fully populated `*ecdsa.PublicKey` *and* a
  non-nil error for which `IsFatal` is false. Before v1.0.21 there was no
  certificate and no key.
* A **bare `SubjectPublicKeyInfo`** handed to `ParsePKIXPublicKey` is still
  refused outright. That entry point has no `NonFatalErrors` channel to
  report a tolerated defect on, so for it the insecure curve stays fatal and
  no key comes back — on exactly the same bytes that succeed inside a
  certificate.

`x509util`'s certificate printer names the curve like any other, as
`ASN1 OID: secp192r1` under a `Public Key: (192 bit)` line. The curve, the
OID constant and the asymmetry are all still present at v1.3.3.

---

## 6. Reading route

1. `rfc/rfc6962.txt` §3.1 — precertificates and the poison extension.
2. §3 of this page — the `NonFatalErrors` contract; then §3.2 for the release
   at which the whole parse family adopts it.
3. §4 — `BuildPrecertTBS`, then ARCHITECTURE.md §5 for where it sits in
   `add-pre-chain`.
4. §2 — the `asn1` laxnesses, for step 8.
5. §5.4 — only if you are at step 13.
6. §5.6 and the "The SAN relaxation at v1.0.17" sub-heading inside §3.1 —
   both arrive at **v1.0.17**, i.e. step 5, and they are the two places this
   page changes that release: the `VerifyOptions` disable switches with the
   leaf-EKU matching rule, and the narrowing of what is fatal in a
   subjectAltName.
