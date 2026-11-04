// Group x509-subject-info-access.
//
// v1.0.21 teaches the forked x509 package the Subject Information Access
// extension of RFC 5280 4.2.2.2 (CHANGELOG v1.0.20: "Support for
// SubjectInfoAccess extension"). Certificate gains the two fields
// SubjectTimestamps and SubjectCARepositories, ParseCertificate fills them
// from the two access methods id-ad-timeStamping and id-ad-caRepository, and
// CreateCertificate emits the extension when a template carries either field.
//
// Observed per function from a driver, as a library capability. Every
// assertion is exact: the extension's OID, its criticality, its complete DER
// value -- rebuilt here with the *standard library's* encoding/asn1, so the
// expected bytes do not come from the code under test -- and the exact string
// slices recovered by a parse. The awkward cases are pinned too: a
// GeneralName that is not a URI is skipped, an unknown access method is
// ignored, and trailing data after the SEQUENCE is a fatal parse error.
//
// SCOPE-TIME: the signing key is a pinned fixture and every validity date is
// a compile-time constant.
//
// v1.0.19 has no SubjectTimestamps or SubjectCARepositories field on
// x509.Certificate and no OIDExtensionSubjectInfoAccess, so this driver does
// not compile against it.
package main

import (
	"crypto"
	"crypto/rand"
	stdx509 "crypto/x509"
	"encoding/asn1"
	"encoding/pem"
	"math/big"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"

	"ctverify/vhelp"
)

// Pinned, clock-free certificate skeleton.
var (
	notBefore = time.Date(2018, time.January, 1, 0, 0, 0, 0, time.UTC)
	notAfter  = time.Date(2028, time.January, 1, 0, 0, 0, 0, time.UTC)
)

const (
	tsOne  = "https://ts.example.com/one"
	tsTwo  = "https://ts.example.com/two"
	repo   = "https://repo.example.com/rpki/"
	repoTw = "rsync://repo.example.com/rpki/"
)

// Standard-library rendering of one AccessDescription, used to build the
// expected DER independently of the package under test.
type accessDesc struct {
	Method   asn1.ObjectIdentifier
	Location asn1.RawValue
}

var (
	oidTimestamp = asn1.ObjectIdentifier{1, 3, 6, 1, 5, 5, 7, 48, 3}
	oidCARepo    = asn1.ObjectIdentifier{1, 3, 6, 1, 5, 5, 7, 48, 5}
	oidBogus     = asn1.ObjectIdentifier{1, 3, 6, 1, 5, 5, 7, 48, 99}
)

func uriName(s string) asn1.RawValue {
	return asn1.RawValue{Tag: 6, Class: asn1.ClassContextSpecific, Bytes: []byte(s)}
}

func dnsName(s string) asn1.RawValue {
	return asn1.RawValue{Tag: 2, Class: asn1.ClassContextSpecific, Bytes: []byte(s)}
}

func marshalSIA(t *vhelp.T, ds ...accessDesc) []byte {
	b, err := asn1.Marshal(ds)
	if err != nil {
		t.Fatalf("encoding/asn1 marshal of SIA: %v", err)
	}
	return b
}

// signer parses the pinned fixture key. Nothing below signs anything the
// group asserts -- the signature is never graded -- but CreateCertificate
// needs a real key pair of the right shape.
func signer(t *vhelp.T) (crypto.Signer, crypto.PublicKey) {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "root-key.pem"))
	if blk == nil {
		t.Fatalf("pinned key fixture is not PEM")
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse pinned key: %v", err)
	}
	s, ok := k.(crypto.Signer)
	if !ok {
		t.Fatalf("pinned key does not implement crypto.Signer")
	}
	return s, s.Public()
}

func main() {
	t := vhelp.New()

	priv, pub := signer(t)

	// ---- the constants themselves --------------------------------------
	oids := strings.Join([]string{
		x509.OIDExtensionSubjectInfoAccess.String(),
		x509.OIDSubjectInfoAccessTimestamp.String(),
		x509.OIDSubjectInfoAccessCARepo.String(),
	}, " ")
	t.Equal("sia-oid-constants", oids,
		"1.3.6.1.5.5.7.1.11 1.3.6.1.5.5.7.48.3 1.3.6.1.5.5.7.48.5")

	// ---- CreateCertificate emits the extension from the template -------
	tmpl := baseTemplate()
	tmpl.SubjectTimestamps = []string{tsOne, tsTwo}
	tmpl.SubjectCARepositories = []string{repo, repoTw}
	cert := roundTrip(t, "both-fields", tmpl, priv, pub)

	var sia []pkix.Extension
	for _, e := range cert.Extensions {
		if e.Id.Equal(x509.OIDExtensionSubjectInfoAccess) {
			sia = append(sia, e)
		}
	}
	t.Equal("create-emits-exactly-one-sia-extension", len(sia), 1)
	if len(sia) != 1 {
		t.Done()
		return
	}
	t.Check("sia-extension-is-not-critical", !sia[0].Critical, "extension was marked critical")

	wantDER := marshalSIA(t,
		accessDesc{Method: oidTimestamp, Location: uriName(tsOne)},
		accessDesc{Method: oidTimestamp, Location: uriName(tsTwo)},
		accessDesc{Method: oidCARepo, Location: uriName(repo)},
		accessDesc{Method: oidCARepo, Location: uriName(repoTw)},
	)
	t.EqualBytes("sia-extension-value-is-exact-der", sia[0].Value, wantDER)

	t.Equal("parse-recovers-subject-timestamps",
		strings.Join(cert.SubjectTimestamps, "|"), tsOne+"|"+tsTwo)
	t.Equal("parse-recovers-subject-ca-repositories",
		strings.Join(cert.SubjectCARepositories, "|"), repo+"|"+repoTw)

	// ---- one field at a time -------------------------------------------
	tmpl = baseTemplate()
	tmpl.SubjectTimestamps = []string{tsOne}
	c := roundTrip(t, "timestamps-only", tmpl, priv, pub)
	t.Equal("timestamps-only-leaves-repositories-empty",
		strings.Join(c.SubjectTimestamps, "|")+"/"+strings.Join(c.SubjectCARepositories, "|"),
		tsOne+"/")

	tmpl = baseTemplate()
	tmpl.SubjectCARepositories = []string{repo}
	c = roundTrip(t, "repositories-only", tmpl, priv, pub)
	t.Equal("repositories-only-leaves-timestamps-empty",
		strings.Join(c.SubjectTimestamps, "|")+"/"+strings.Join(c.SubjectCARepositories, "|"),
		"/"+repo)

	// ---- neither field: no extension at all ----------------------------
	c = roundTrip(t, "no-sia", baseTemplate(), priv, pub)
	found := false
	for _, e := range c.Extensions {
		if e.Id.Equal(x509.OIDExtensionSubjectInfoAccess) {
			found = true
		}
	}
	t.Check("no-sia-extension-without-template-fields", !found,
		"an SIA extension appeared for a template that asked for none")

	// ---- a GeneralName that is not a URI is skipped --------------------
	tmpl = baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{{
		Id: x509.OIDExtensionSubjectInfoAccess,
		Value: marshalSIA(t,
			accessDesc{Method: oidTimestamp, Location: dnsName("ts.example.com")},
			accessDesc{Method: oidCARepo, Location: uriName(repo)}),
	}}
	c = roundTrip(t, "non-uri-name", tmpl, priv, pub)
	t.Equal("parse-skips-non-uri-general-name",
		strings.Join(c.SubjectTimestamps, "|")+"/"+strings.Join(c.SubjectCARepositories, "|"),
		"/"+repo)

	// ---- an unknown access method is ignored ---------------------------
	tmpl = baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{{
		Id: x509.OIDExtensionSubjectInfoAccess,
		Value: marshalSIA(t,
			accessDesc{Method: oidBogus, Location: uriName("https://unknown.example.com/")},
			accessDesc{Method: oidTimestamp, Location: uriName(tsTwo)}),
	}}
	c = roundTrip(t, "unknown-method", tmpl, priv, pub)
	t.Equal("parse-ignores-unknown-access-method",
		strings.Join(c.SubjectTimestamps, "|")+"/"+strings.Join(c.SubjectCARepositories, "|"),
		tsTwo+"/")

	// ---- trailing data after the SEQUENCE is fatal ---------------------
	tmpl = baseTemplate()
	tmpl.ExtraExtensions = []pkix.Extension{{
		Id:    x509.OIDExtensionSubjectInfoAccess,
		Value: append(marshalSIA(t, accessDesc{Method: oidCARepo, Location: uriName(repo)}), 0x00),
	}}
	der := create(t, "trailing-data", tmpl, priv, pub)
	bad, err := x509.ParseCertificate(der)
	t.Check("trailing-data-after-sia-is-fatal", err != nil && bad == nil,
		"a trailing byte after the SIA sequence parsed cleanly: err=%v", err)

	t.Done()
}

// baseTemplate is the pinned skeleton every case starts from.
func baseTemplate() *x509.Certificate {
	return &x509.Certificate{
		SerialNumber:          big.NewInt(0x5ec7),
		Subject:               pkix.Name{CommonName: "harbor-ct-bench sia subject"},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		BasicConstraintsValid: true,
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageCertSign,
	}
}

func create(t *vhelp.T, what string, tmpl *x509.Certificate, priv crypto.Signer, pub crypto.PublicKey) []byte {
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate(%s): %v", what, err)
	}
	return der
}

func roundTrip(t *vhelp.T, what string, tmpl *x509.Certificate, priv crypto.Signer, pub crypto.PublicKey) *x509.Certificate {
	c, err := x509.ParseCertificate(create(t, what, tmpl, priv, pub))
	if err != nil {
		t.Fatalf("ParseCertificate(%s): %v", what, err)
	}
	return c
}
