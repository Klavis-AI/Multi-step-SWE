// Group inherited-x509-fork-nonfatal.
//
// Two contracts the forked ./x509 has carried since step 1 and still has at
// v1.1.4. First, lax parsing: a certificate that breaks DER or RFC 5280 in a
// way a log must still be able to record is parsed anyway, the breakage is
// reported as a non-fatal error, and the certificate that comes back is fully
// populated -- x509.IsFatal is the exported boundary between "malformed, but
// loggable" and "not a certificate". Second, the CT-specific extensions the
// fork exists for: the poison marker that makes a pre-certificate, and the
// SCT-list extension it parses into typed fields.
//
// ./x509 differs between v1.1.3 and v1.1.4 only in build tags, ioutil->os and
// gofmt'ed comments -- no parsing rule moves -- so this is an inherited
// regression group and passes at both anchors.
//
// Nothing here grades an error string or an error type: the classification is
// x509.IsFatal, and the evidence that parsing really continued is the exact
// serial numbers, extension OIDs and SCT fields recovered afterwards.
package main

import (
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"sort"
	"strings"

	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const (
	poisonOID  = "1.3.6.1.4.1.11129.2.4.3"
	sctListOID = "1.3.6.1.4.1.11129.2.4.2"
)

// the malformed corpus, in the order asserted below.
var malformed = []string{
	"xf-der-invalid-nonminimal-int.pem",
	"xf-der-pubkey-rsa-nonminimal-int.pem",
	"xf-ext-extended-key-usage-empty-oid.pem",
	"xf-ext-extended-key-usage-empty.pem",
	"xf-ext-subject-info-empty.pem",
	"xf-pubkey-ecdsa-secp192r1.pem",
	"xf-pubkey-rsa-modulus-negative.pem",
	"xf-pubkey-rsa-param-nonnull.pem",
	"xf-subject-nonprintable.pem",
}

func pemDER(t *vhelp.T, rel ...string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture(rel...))
	if blk == nil {
		t.Fatalf("fixture %v is not PEM", rel)
	}
	return blk.Bytes
}

func oidsOf(c *x509.Certificate) string {
	out := make([]string, 0, len(c.Extensions))
	for _, e := range c.Extensions {
		out = append(out, e.Id.String())
	}
	sort.Strings(out)
	return strings.Join(out, " ")
}

func main() {
	t := vhelp.New()

	// --- 1. a well-formed certificate takes the silent path -----------------
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	leaf, err := x509.ParseCertificate(leafDER)
	t.Check("a-well-formed-certificate-parses-with-no-error-at-all",
		err == nil && leaf != nil && leaf.SerialNumber.String() == "4097",
		"err=%v cert==nil=%v", err, leaf == nil)
	t.Check("no-error-is-not-a-fatal-error", !x509.IsFatal(nil), "IsFatal(nil) was true")

	// --- 2. the malformed corpus is loggable, not discarded ------------------
	// Every fixture must parse non-fatally and yield its serial number: that
	// serial is only recoverable if parsing really continued past the defect.
	var serials []string
	nonFatal, reported := 0, 0
	for _, name := range malformed {
		c, perr := x509.ParseCertificate(pemDER(t, "malformed", name))
		if !x509.IsFatal(perr) && c != nil {
			nonFatal++
			serials = append(serials, c.SerialNumber.String())
		} else {
			serials = append(serials, "FATAL("+name+")")
		}
		if perr != nil {
			reported++
		}
	}
	t.Equal("every-malformed-fixture-is-non-fatal", nonFatal, len(malformed))
	t.Equal("every-malformed-fixture-yields-its-serial-number", strings.Join(serials, ","),
		"4660,4660,140298796799689226,96096843357863639,143775503077481624,"+
			"141222174073305197,129201852361993560,103563476275448133,116666066052028467")
	// Laxness is not silence: each defect is still reported, just not fatally.
	t.Equal("every-malformed-fixture-still-reports-an-error", reported, len(malformed))

	// --- 3. the floor: laxness has a limit ---------------------------------
	full := pemDER(t, "malformed", "xf-subject-nonprintable.pem")
	half, errHalf := x509.ParseCertificate(full[:len(full)/2])
	t.Check("truncated-der-is-fatal", x509.IsFatal(errHalf) && half == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(errHalf), half == nil)
	empty, errEmpty := x509.ParseCertificate(nil)
	t.Check("empty-input-is-fatal", x509.IsFatal(errEmpty) && empty == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(errEmpty), empty == nil)
	// A SubjectPublicKeyInfo is valid DER and is not a certificate.
	spki := leaf.RawSubjectPublicKeyInfo
	notCert, errNotCert := x509.ParseCertificate(spki)
	t.Check("valid-der-that-is-not-a-certificate-is-fatal",
		x509.IsFatal(errNotCert) && notCert == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(errNotCert), notCert == nil)

	// --- 4. the plural entry point -----------------------------------------
	both, errBoth := x509.ParseCertificates(append(append([]byte{}, leafDER...), intDER...))
	if x509.IsFatal(errBoth) || len(both) != 2 {
		t.Check("concatenated-der-parses-into-both-certificates", false,
			"err=%v n=%d", errBoth, len(both))
	} else {
		t.Equal("concatenated-der-parses-into-both-certificates",
			both[0].SerialNumber.String()+","+both[1].SerialNumber.String()+","+
				both[1].Subject.CommonName,
			"4097,2,Harbor CT Bench Intermediate")
	}

	// --- 5. the CT poison marker -------------------------------------------
	poisoned, perr := x509.ParseCertificate(vhelp.ReadFixture("minted", "precert-poisoned.der"))
	if x509.IsFatal(perr) || poisoned == nil {
		t.Fatalf("precert-poisoned.der did not parse: %v", perr)
	}
	plain, nerr := x509.ParseCertificate(vhelp.ReadFixture("minted", "precert-nopoison.der"))
	if x509.IsFatal(nerr) || plain == nil {
		t.Fatalf("precert-nopoison.der did not parse: %v", nerr)
	}
	t.Check("the-poison-extension-is-what-makes-a-precertificate",
		poisoned.IsPrecertificate() && !plain.IsPrecertificate() && !leaf.IsPrecertificate(),
		"poisoned=%v nopoison=%v leaf=%v",
		poisoned.IsPrecertificate(), plain.IsPrecertificate(), leaf.IsPrecertificate())
	t.Check("a-nil-certificate-is-not-a-precertificate",
		!(*x509.Certificate)(nil).IsPrecertificate(), "nil was a precertificate")
	t.Equal("the-poisoned-and-plain-precertificates-differ-by-exactly-the-poison-oid",
		strings.TrimSpace(strings.Replace(oidsOf(poisoned), oidsOf(plain), "", 1)), poisonOID)

	// Removing the poison from the TBS leaves a parseable TBSCertificate with
	// the same serial and one extension fewer, and the operation is refused on
	// a TBS that never had one.
	defanged, derr := x509.RemoveCTPoison(poisoned.RawTBSCertificate)
	_, plainErr := x509.RemoveCTPoison(plain.RawTBSCertificate)
	if derr != nil {
		t.Check("remove-ct-poison-rebuilds-the-tbs", false, "RemoveCTPoison: %v", derr)
	} else {
		tbs, terr := x509.ParseTBSCertificate(defanged)
		t.Check("remove-ct-poison-rebuilds-the-tbs",
			!x509.IsFatal(terr) && tbs != nil && !tbs.IsPrecertificate() &&
				tbs.SerialNumber.String() == poisoned.SerialNumber.String() &&
				len(tbs.Extensions) == len(poisoned.Extensions)-1,
			"err=%v tbs==nil=%v", terr, tbs == nil)
	}
	t.Check("remove-ct-poison-is-refused-on-an-unpoisoned-tbs", plainErr != nil,
		"RemoveCTPoison accepted a TBS with no poison extension")

	// --- 6. the SCT list extension ------------------------------------------
	embedded, eerr := x509.ParseCertificate(pemDER(t, "rfc6962", "test-embedded-cert.pem"))
	if x509.IsFatal(eerr) || embedded == nil {
		t.Fatalf("test-embedded-cert.pem did not parse: %v", eerr)
	}
	t.Check("an-embedded-sct-list-is-parsed-into-typed-entries",
		len(embedded.SCTList.SCTList) == 1 && len(embedded.RawSCT) > 0 &&
			strings.Contains(oidsOf(embedded), sctListOID),
		"n=%d raw=%d oids=%s", len(embedded.SCTList.SCTList), len(embedded.RawSCT), oidsOf(embedded))

	if len(embedded.SCTList.SCTList) == 1 {
		// The serialized entry is a v1 SCT and decodes to exact field values.
		var sct struct {
			Version   uint8
			LogID     [32]byte
			Timestamp uint64
		}
		body := embedded.SCTList.SCTList[0].Val
		if len(body) < 41 {
			t.Check("the-embedded-sct-decodes-to-exact-field-values", false, "entry is %d bytes", len(body))
		} else {
			sct.Version = body[0]
			copy(sct.LogID[:], body[1:33])
			for _, b := range body[33:41] {
				sct.Timestamp = sct.Timestamp<<8 | uint64(b)
			}
			t.Equal("the-embedded-sct-decodes-to-exact-field-values",
				fmt.Sprintf("%d/%s/%d", sct.Version, hex.EncodeToString(sct.LogID[:]), sct.Timestamp),
				"0/df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764/1365181456275")
		}
		// The whole extension round-trips through the presentation language.
		reWire, rerr := tls.Marshal(embedded.SCTList)
		t.Check("the-sct-list-re-encodes-to-the-bytes-it-was-parsed-from",
			rerr == nil && hex.EncodeToString(reWire) == hex.EncodeToString(embedded.RawSCT),
			"err=%v got=%x want=%x", rerr, reWire, embedded.RawSCT)
	} else {
		t.Check("the-embedded-sct-decodes-to-exact-field-values", false, "no SCT to decode")
		t.Check("the-sct-list-re-encodes-to-the-bytes-it-was-parsed-from", false, "no SCT list")
	}

	// Removing the SCT list is what rebuilds the leaf a log signed, and a
	// certificate that never carried one has an empty list.
	noSCT, rerr := x509.RemoveSCTList(embedded.RawTBSCertificate)
	if rerr != nil {
		t.Check("remove-sct-list-rebuilds-the-signed-tbs", false, "RemoveSCTList: %v", rerr)
	} else {
		tbs, terr := x509.ParseTBSCertificate(noSCT)
		t.Check("remove-sct-list-rebuilds-the-signed-tbs",
			!x509.IsFatal(terr) && tbs != nil && len(tbs.SCTList.SCTList) == 0 &&
				len(noSCT) < len(embedded.RawTBSCertificate) &&
				!strings.Contains(oidsOf(tbs), sctListOID),
			"err=%v shorter=%v", terr, len(noSCT) < len(embedded.RawTBSCertificate))
	}
	t.Check("a-certificate-without-an-sct-list-has-an-empty-one",
		len(leaf.SCTList.SCTList) == 0 && len(leaf.RawSCT) == 0,
		"n=%d raw=%d", len(leaf.SCTList.SCTList), len(leaf.RawSCT))

	t.Done()
}
