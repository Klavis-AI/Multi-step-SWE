// Observes the public/private key consistency rule this release adds to log
// set-up, and the Instance.GetPublicKey accessor that goes with it. A log
// whose configured public key does not belong to its configured private key
// would sign every SCT with a key no verifier could find, so set-up must
// refuse it outright rather than start.
//
// SCOPE-TIME: every key is a pinned fixture; nothing is generated here.
package main

import (
	"context"
	"crypto"
	stdx509 "crypto/x509"
	"encoding/pem"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keys"
	"github.com/google/trillian/crypto/keys/der"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

// spkiOf returns the DER SubjectPublicKeyInfo of the public half of a pinned
// PKCS#8 private key fixture.
func spkiOf(t *vhelp.T, rel ...string) []byte {
	k, err := stdx509.ParsePKCS8PrivateKey(pemBody(t, vhelp.ReadFixture(rel...)))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	pub, ok := k.(interface{ Public() crypto.PublicKey })
	if !ok {
		t.Fatalf("%v is not a signer", rel)
	}
	b, err := stdx509.MarshalPKIXPublicKey(pub.Public())
	if err != nil {
		t.Fatalf("marshal public key of %v: %v", rel, err)
	}
	return b
}

func config(t *vhelp.T, pubDER []byte) *configpb.LogConfig {
	priv, err := anypb.New(&keyspb.PrivateKey{Der: pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))})
	if err != nil {
		t.Fatalf("wrap private key: %v", err)
	}
	cfg := &configpb.LogConfig{
		LogId:        1001,
		Prefix:       "bench",
		RootsPemFile: []string{vhelp.Fixture("minted", "roots-bench.pem")},
		PrivateKey:   priv,
	}
	if pubDER != nil {
		cfg.PublicKey = &keyspb.PublicKey{Der: pubDER}
	}
	return cfg
}

func setUp(t *vhelp.T, cfg *configpb.LogConfig) (*ctfe.Instance, error) {
	v, err := ctfe.ValidateLogConfig(cfg)
	if err != nil {
		return nil, err
	}
	return ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:     v,
		Deadline:      time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    &ctfe.DefaultRequestLog{},
	})
}

func main() {
	t := vhelp.New()
	keys.RegisterHandler(&keyspb.PrivateKey{}, der.FromProto)

	logSPKI := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	inst, err := setUp(t, config(t, logSPKI))
	t.Check("matching-public-key-accepted", err == nil && inst != nil,
		"set-up with the log's own public key = (%v, %v), want an instance", inst, err)

	// GetPublicKey must hand back the signer's public key, byte for byte the
	// configured SubjectPublicKeyInfo.
	var gotSPKI []byte
	if inst != nil {
		if pub := inst.GetPublicKey(); pub != nil {
			gotSPKI, _ = stdx509.MarshalPKIXPublicKey(pub)
		}
	}
	t.EqualBytes("get-public-key-is-the-signer-public-key", gotSPKI, logSPKI)

	t.Check("get-public-key-of-a-bare-instance-is-nil", (&ctfe.Instance{}).GetPublicKey() == nil,
		"Instance{}.GetPublicKey() was not nil")

	// A different ECDSA key: same type, wrong key.
	inst, err = setUp(t, config(t, spkiOf(t, "minted", "keys", "root-key.pem")))
	t.Check("mismatched-ecdsa-public-key-rejected", inst == nil && err != nil,
		"set-up with a foreign ECDSA public key = (%v, %v), want (<nil>, <error>)", inst, err)

	inst, err = setUp(t, config(t, spkiOf(t, "minted", "keys", "intermediate-key.pem")))
	t.Check("second-mismatched-ecdsa-public-key-rejected", inst == nil && err != nil,
		"set-up with a second foreign ECDSA public key = (%v, %v), want (<nil>, <error>)", inst, err)

	// An RSA public key can never match an ECDSA signer.
	inst, err = setUp(t, config(t, pemBody(t, vhelp.ReadFixture("keys", "rsa-public-key.pem"))))
	t.Check("rsa-public-key-with-ecdsa-signer-rejected", inst == nil && err != nil,
		"set-up with an RSA public key = (%v, %v), want (<nil>, <error>)", inst, err)

	// A key type outside the three the cross-check knows about. CONFIG.md
	// section 2.3 separates this from a mismatch: ECDSA, Ed25519 and RSA are
	// compared against the signer's public half, and anything else is refused
	// for being uncomparable. DSA is parseable by the forked x509 -- so the
	// configuration is well formed and reaches set-up -- and is not one of the
	// three, so set-up must refuse it rather than accept it unchecked.
	inst, err = setUp(t, config(t, pemBody(t, vhelp.ReadFixture("keys", "dsa-public-key.pem"))))
	t.Check("public-key-outside-the-three-types-rejected", inst == nil && err != nil,
		"set-up with a DSA public key = (%v, %v), want (<nil>, <error>)", inst, err)

	// No configured public key at all: the check is opt-in and must not
	// break a log that only configures a private key.
	inst, err = setUp(t, config(t, nil))
	t.Check("absent-public-key-still-accepted", err == nil && inst != nil,
		"set-up with no public key = (%v, %v), want an instance", inst, err)

	t.Done()
}
