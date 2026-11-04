// Group asn1-lax-oid-and-integer.
//
// v1.1.0 documents three named laxnesses in the forked asn1 package and gates
// all three behind a new `lax` struct-tag component that
// asn1.UnmarshalWithParams turns on. Two of them are exercised here:
//
//	checkInteger()          accepts a non-minimally-encoded INTEGER
//	parseObjectIdentifier() accepts a zero-length OBJECT IDENTIFIER
//
// and the boundary of the first: an EMPTY integer is refused whether or not
// the tag is set, so `lax` is a named list of recoveries and not a switch that
// stops checking integers.
//
// At v1.0.21 there is no `lax` tag at all: `asn1.UnmarshalWithParams(b, v,
// "lax")` behaves exactly like strict parsing, so every "lax-..." case below
// still reports the strict error. Every input is a pinned DER/BER byte string
// written out in full, so nothing depends on a fixture or a clock.
package main

import (
	"math/big"
	"strings"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/asn1"
)

// Pinned encodings. Each is a SEQUENCE wrapping one offending element.
var (
	// SEQUENCE { OBJECT IDENTIFIER (0 bytes) }
	zeroOID = []byte{0x30, 0x02, 0x06, 0x00}
	// SEQUENCE { INTEGER 00 01 }  -- 1, encoded on two bytes
	nonMinimalInt = []byte{0x30, 0x04, 0x02, 0x02, 0x00, 0x01}
	// SEQUENCE { SEQUENCE { INTEGER 00 01 } }
	nestedNonMinimalInt = []byte{0x30, 0x06, 0x30, 0x04, 0x02, 0x02, 0x00, 0x01}
	// SEQUENCE { SEQUENCE OF { INTEGER 00 01, INTEGER 02 } }
	seqOfNonMinimalInt = []byte{0x30, 0x09, 0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x02}
	// SEQUENCE { SEQUENCE OF { OBJECT IDENTIFIER (0 bytes), OBJECT IDENTIFIER 2.5.4.3 } }
	seqOfZeroOID = []byte{0x30, 0x09, 0x30, 0x07, 0x06, 0x00, 0x06, 0x03, 0x55, 0x04, 0x03}
	// SEQUENCE { INTEGER 42 }  -- perfectly minimal
	minimalInt = []byte{0x30, 0x03, 0x02, 0x01, 0x2a}
	// SEQUENCE claiming four content bytes but holding three
	truncated = []byte{0x30, 0x04, 0x02, 0x02, 0x00}
	// SEQUENCE { INTEGER (0 content bytes) }
	emptyInt = []byte{0x30, 0x02, 0x02, 0x00}
)

type oidHolder struct{ OID asn1.ObjectIdentifier }
type intHolder struct{ N int64 }
type bigHolder struct{ N *big.Int }
type innerInt struct{ N int64 }
type nestedHolder struct{ Inner innerInt }
type seqIntHolder struct{ L []int64 }
type seqOIDHolder struct{ L []asn1.ObjectIdentifier }

func errString(err error) string {
	if err == nil {
		return "<nil>"
	}
	return err.Error()
}

func main() {
	t := vhelp.New()

	// --- zero-length OBJECT IDENTIFIER ---------------------------------------
	var v oidHolder
	_, err := asn1.Unmarshal(zeroOID, &v)
	t.Check("strict-zero-length-oid-rejected", err != nil && len(v.OID) == 0,
		"err=%s oid=%v; want an error and no value", errString(err), v.OID)

	v = oidHolder{}
	rest, err := asn1.UnmarshalWithParams(zeroOID, &v, "lax")
	t.Check("lax-zero-length-oid-accepted", err == nil && len(rest) == 0 && len(v.OID) == 0,
		"err=%s rest=%d oid=%v", errString(err), len(rest), v.OID)

	var vs seqOIDHolder
	_, err = asn1.Unmarshal(seqOfZeroOID, &vs)
	t.Check("strict-zero-length-oid-in-sequence-of-rejected", err != nil,
		"err=%s value=%v", errString(err), vs.L)

	vs = seqOIDHolder{}
	_, err = asn1.UnmarshalWithParams(seqOfZeroOID, &vs, "lax")
	got := ""
	if err == nil && len(vs.L) == 2 {
		got = vs.L[0].String() + "|" + vs.L[1].String()
	} else {
		got = "err=" + errString(err)
	}
	// An empty ObjectIdentifier renders as the empty string.
	t.Equal("lax-zero-length-oid-in-sequence-of-accepted", got, "|2.5.4.3")

	// --- non-minimally-encoded INTEGER ---------------------------------------
	var n intHolder
	_, err = asn1.Unmarshal(nonMinimalInt, &n)
	t.Check("strict-non-minimal-integer-rejected", err != nil && n.N == 0,
		"err=%s n=%d; want an error and no value", errString(err), n.N)

	n = intHolder{}
	_, err = asn1.UnmarshalWithParams(nonMinimalInt, &n, "lax")
	t.Check("lax-non-minimal-integer-accepted", err == nil && n.N == 1,
		"err=%s n=%d", errString(err), n.N)

	var b bigHolder
	_, err = asn1.UnmarshalWithParams(nonMinimalInt, &b, "lax")
	t.Check("lax-non-minimal-big-integer-accepted",
		err == nil && b.N != nil && b.N.Cmp(big.NewInt(1)) == 0,
		"err=%s n=%v", errString(err), b.N)

	// The tag is inherited by nested structures, not only applied to the
	// outermost field.
	var nest nestedHolder
	_, err = asn1.Unmarshal(nestedNonMinimalInt, &nest)
	t.Check("strict-non-minimal-integer-nested-rejected", err != nil,
		"err=%s n=%d", errString(err), nest.Inner.N)

	nest = nestedHolder{}
	_, err = asn1.UnmarshalWithParams(nestedNonMinimalInt, &nest, "lax")
	t.Check("lax-non-minimal-integer-nested-accepted", err == nil && nest.Inner.N == 1,
		"err=%s n=%d", errString(err), nest.Inner.N)

	// ... and by the elements of a SEQUENCE OF.
	var sl seqIntHolder
	_, err = asn1.UnmarshalWithParams(seqOfNonMinimalInt, &sl, "lax")
	t.Check("lax-non-minimal-integer-in-sequence-of-accepted",
		err == nil && len(sl.L) == 2 && sl.L[0] == 1 && sl.L[1] == 2,
		"err=%s l=%v", errString(err), sl.L)

	// --- what lax must NOT do ------------------------------------------------
	// A well-formed encoding parses identically with and without the tag.
	var a, c intHolder
	_, errA := asn1.Unmarshal(minimalInt, &a)
	_, errC := asn1.UnmarshalWithParams(minimalInt, &c, "lax")
	t.Check("lax-leaves-valid-encoding-unchanged",
		errA == nil && errC == nil && a.N == 42 && c.N == 42,
		"strict err=%s n=%d; lax err=%s n=%d", errString(errA), a.N, errString(errC), c.N)

	// Laxness is a whitelist of three recoveries, not "ignore all errors":
	// truncated data is still a hard failure.
	var tr intHolder
	_, err = asn1.UnmarshalWithParams(truncated, &tr, "lax")
	t.Check("lax-still-rejects-truncated-data", err != nil && tr.N == 0,
		"err=%s n=%d; want an error and no value", errString(err), tr.N)

	// The boundary of relaxation 2, from both sides. X509-FORK.md §2.2: "A
	// single-byte integer is always accepted; an empty integer is always
	// StructuralError{"empty integer"}, lax or not." The single-byte half is
	// minimalInt just above; this is the empty half. It is the one INTEGER
	// input the tag must not change the answer for -- the emptiness test in
	// checkInteger sits in front of the lax short-circuit, so a `lax` that
	// short-circuits first would turn a zero-length INTEGER into the value 0
	// instead of an error. Only the documented message substring is compared;
	// nothing here reads the error's Field.
	var e0 intHolder
	errEmpty := func(err error) bool {
		return err != nil && strings.Contains(err.Error(), "empty integer")
	}
	_, errE := asn1.Unmarshal(emptyInt, &e0)
	t.Check("strict-empty-integer-rejected", errEmpty(errE) && e0.N == 0,
		"err=%s n=%d; want an \"empty integer\" error and no value", errString(errE), e0.N)

	var e1 intHolder
	_, errE = asn1.UnmarshalWithParams(emptyInt, &e1, "lax")
	t.Check("lax-empty-integer-still-rejected", errEmpty(errE) && e1.N == 0,
		"err=%s n=%d; want an \"empty integer\" error and no value", errString(errE), e1.N)

	t.Done()
}
