// Group ctfe-validate-log-config-mirror.
//
// v1.0.21 teaches ctfe.ValidateLogConfig what a mirror is (CONFIG.md section
// 2.5): a mirror must carry a public key and must not carry a private one,
// while a non-mirror still needs its private key. The same release adds the
// two advertised merge delays and validates them as promises -- neither may
// be negative and the expected delay may not exceed the maximum -- without
// enforcing either.
//
// v1.0.21 is also the release that *concentrates* per-log validation in this
// one exported function. Two rule families that used to live elsewhere move
// in with it and are graded here: the extended-key-usage name table
// (CONFIG.md section 2.4 -- an unrecognised name is refused, an empty list is
// rewritten to the one-element slice [ExtKeyUsageAny] at this release) and
// the half-open not_after window (each endpoint must be a valid timestamp and
// the limit must not precede the start). The whole-file validator
// ValidateLogMultiConfig keeps its own rules but its second result becomes
// the named LogBackendMap at this release (CONFIG.md section 4.1).
//
// Observed at the exported functions the documentation names, with pinned key
// material from the fixture corpus. Only the presence or absence of an error
// is graded, never its text.
//
// v1.0.19's LogConfig has no IsMirror, MaxMergeDelaySec or
// ExpectedMergeDelaySec field, and v1.0.19's ctfe exports neither
// ValidateLogConfig nor LogBackendMap, so this driver does not compile
// against that tree.
package main

import (
	"encoding/pem"
	"fmt"

	"github.com/golang/protobuf/ptypes"
	any "github.com/golang/protobuf/ptypes/any"
	tspb "github.com/golang/protobuf/ptypes/timestamp"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/trillian/crypto/keyspb"

	"ctverify/vhelp"
)

// ts is a pinned protobuf timestamp. SCOPE-TIME: no clock is read here; both
// values below are compile-time constants chosen so that one precedes the
// other.
func ts(sec int64) *tspb.Timestamp { return &tspb.Timestamp{Seconds: sec} }

const (
	pinnedWindowStart = int64(1500000000) // 2017-07-14T02:40:00Z
	pinnedWindowLimit = int64(1600000000) // 2020-09-13T12:26:40Z
)

// renderEKUs renders a validated config's key-usage slice so a mismatch names
// every entry rather than just the first.
func renderEKUs(kus []x509.ExtKeyUsage) string {
	out := make([]string, 0, len(kus))
	for _, ku := range kus {
		out = append(out, fmt.Sprintf("%d", int(ku)))
	}
	return fmt.Sprintf("%v", out)
}

const pinnedLogID = int64(1001)

func pemBody(t *vhelp.T, rel ...string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture(rel...))
	if blk == nil {
		t.Fatalf("fixture %v is not PEM", rel)
	}
	return blk.Bytes
}

func main() {
	t := vhelp.New()

	privDER := pemBody(t, "minted", "keys", "log-key.pem")
	pubDER := pemBody(t, "minted", "keys", "log-pubkey.pem")

	privAny, err := ptypes.MarshalAny(&keyspb.PrivateKey{Der: privDER})
	if err != nil {
		t.Fatalf("wrap pinned private key: %v", err)
	}
	pub := &keyspb.PublicKey{Der: pubDER}

	// cfg builds a config from the pieces each case wants to vary.
	cfg := func(mirror bool, priv *any.Any, pubKey *keyspb.PublicKey, mmd, emd int32) *configpb.LogConfig {
		return &configpb.LogConfig{
			LogId:                 pinnedLogID,
			Prefix:                "bench",
			RootsPemFile:          []string{vhelp.Fixture("minted", "roots-bench.pem")},
			IsMirror:              mirror,
			PrivateKey:            priv,
			PublicKey:             pubKey,
			MaxMergeDelaySec:      mmd,
			ExpectedMergeDelaySec: emd,
		}
	}

	accept := func(id string, c *configpb.LogConfig) *ctfe.ValidatedLogConfig {
		v, err := ctfe.ValidateLogConfig(c)
		t.Check(id, err == nil && v != nil, "config was refused: %v", err)
		return v
	}
	refuse := func(id string, c *configpb.LogConfig) {
		v, err := ctfe.ValidateLogConfig(c)
		t.Check(id, err != nil && v == nil, "config was accepted")
	}

	// --- the shape of a normal log is unchanged ------------------------------
	plain := cfg(false, privAny, nil, 0, 0)
	if vp := accept("non-mirror-with-private-key-accepted", plain); vp != nil {
		// The last two fields of the validated struct. Config is the very
		// config that was handed in -- the same pointer, not a copy -- and
		// PrivKey is the Any unwrapped into its concrete message. Together
		// they are why the caller is told to reuse this struct instead of
		// validating a second time.
		key, _ := vp.PrivKey.Message.(*keyspb.PrivateKey)
		t.Equal("validated-config-carries-the-config-and-the-unwrapped-private-key",
			fmt.Sprintf("same-config=%v priv=%T der-match=%v",
				vp.Config == plain, vp.PrivKey.Message,
				key != nil && string(key.Der) == string(privDER)),
			"same-config=true priv=*keyspb.PrivateKey der-match=true")
	} else {
		t.Check("validated-config-carries-the-config-and-the-unwrapped-private-key",
			false, "validation returned nil")
	}
	refuse("non-mirror-without-private-key-refused", cfg(false, nil, pub, 0, 0))

	// --- what a mirror requires and what it forbids ---------------------------
	v := accept("mirror-with-public-key-accepted", cfg(true, nil, pub, 0, 0))
	if v != nil {
		t.Check("mirror-validated-config-carries-public-key", v.PubKey != nil,
			"a validated mirror config has no parsed public key")
	} else {
		t.Check("mirror-validated-config-carries-public-key", false, "validation returned nil")
	}
	refuse("mirror-with-private-key-refused", cfg(true, privAny, pub, 0, 0))
	refuse("mirror-without-public-key-refused", cfg(true, nil, nil, 0, 0))

	// A mirror needs no roots file: it never admits a chain.
	mirrorNoRoots := cfg(true, nil, pub, 0, 0)
	mirrorNoRoots.RootsPemFile = nil
	accept("mirror-without-roots-accepted", mirrorNoRoots)

	// --- the two advertised merge delays --------------------------------------
	accept("merge-delays-unset-accepted", cfg(false, privAny, nil, 0, 0))
	accept("merge-delays-expected-below-max-accepted", cfg(false, privAny, nil, 86400, 120))
	accept("merge-delays-expected-equals-max-accepted", cfg(false, privAny, nil, 86400, 86400))
	refuse("merge-delays-expected-above-max-refused", cfg(false, privAny, nil, 120, 86400))
	refuse("merge-delays-negative-max-refused", cfg(false, privAny, nil, -1, 0))
	refuse("merge-delays-negative-expected-refused", cfg(false, privAny, nil, 0, -1))

	// --- the extended-key-usage name table (CONFIG.md 2.4) --------------------
	// An unrecognised name is refused outright; recognised names survive into
	// the validated config in the order they were written.
	badEKU := cfg(false, privAny, nil, 0, 0)
	badEKU.ExtKeyUsages = []string{"ServerAuth", "HarborNotAKeyUsage"}
	refuse("unknown-ext-key-usage-refused", badEKU)

	goodEKU := cfg(false, privAny, nil, 0, 0)
	goodEKU.ExtKeyUsages = []string{"ServerAuth", "ClientAuth", "OCSPSigning"}
	if v := accept("known-ext-key-usages-accepted", goodEKU); v != nil {
		t.Equal("validated-config-carries-the-parsed-key-usages",
			renderEKUs(v.KeyUsages),
			renderEKUs([]x509.ExtKeyUsage{
				x509.ExtKeyUsageServerAuth,
				x509.ExtKeyUsageClientAuth,
				x509.ExtKeyUsageOCSPSigning,
			}))
	} else {
		t.Check("validated-config-carries-the-parsed-key-usages", false,
			"validation returned nil")
	}

	// An empty list is not "no filter" at this release: it is rewritten to the
	// one-element slice [ExtKeyUsageAny].
	if v := accept("empty-ext-key-usages-accepted", cfg(false, privAny, nil, 0, 0)); v != nil {
		t.Equal("empty-ext-key-usage-list-becomes-any",
			renderEKUs(v.KeyUsages),
			renderEKUs([]x509.ExtKeyUsage{x509.ExtKeyUsageAny}))
	} else {
		t.Check("empty-ext-key-usage-list-becomes-any", false, "validation returned nil")
	}

	// --- the half-open not_after window (CONFIG.md 2.4) -----------------------
	// A nanos field outside [0, 1e9) is not a representable instant, so the
	// endpoint carrying it is an invalid timestamp. Each endpoint is checked
	// on its own, then the two are checked against each other.
	badStart := cfg(false, privAny, nil, 0, 0)
	badStart.NotAfterStart = &tspb.Timestamp{Seconds: pinnedWindowStart, Nanos: -1}
	refuse("not-after-start-invalid-timestamp-refused", badStart)

	badLimit := cfg(false, privAny, nil, 0, 0)
	badLimit.NotAfterLimit = &tspb.Timestamp{Seconds: pinnedWindowLimit, Nanos: -1}
	refuse("not-after-limit-invalid-timestamp-refused", badLimit)

	inverted := cfg(false, privAny, nil, 0, 0)
	inverted.NotAfterStart = ts(pinnedWindowLimit)
	inverted.NotAfterLimit = ts(pinnedWindowStart)
	refuse("not-after-limit-before-start-refused", inverted)

	window := cfg(false, privAny, nil, 0, 0)
	window.NotAfterStart = ts(pinnedWindowStart)
	window.NotAfterLimit = ts(pinnedWindowLimit)
	if v := accept("not-after-window-in-order-accepted", window); v != nil {
		got := "nil/nil"
		if v.NotAfterStart != nil && v.NotAfterLimit != nil {
			got = fmt.Sprintf("%d/%d", v.NotAfterStart.Unix(), v.NotAfterLimit.Unix())
		}
		t.Equal("validated-config-carries-the-not-after-window", got,
			fmt.Sprintf("%d/%d", pinnedWindowStart, pinnedWindowLimit))
	} else {
		t.Check("validated-config-carries-the-not-after-window", false,
			"validation returned nil")
	}

	// A window endpoint left unset stays nil rather than becoming a zero time,
	// so "half-open" really is optional on both sides.
	if v := accept("not-after-window-omitted-accepted", cfg(false, privAny, nil, 0, 0)); v != nil {
		t.Equal("omitted-not-after-window-leaves-both-endpoints-nil",
			fmt.Sprintf("start=%v limit=%v", v.NotAfterStart == nil, v.NotAfterLimit == nil),
			"start=true limit=true")
	} else {
		t.Check("omitted-not-after-window-leaves-both-endpoints-nil", false,
			"validation returned nil")
	}

	// --- the whole-file validator's named result type (CONFIG.md 4.1) ---------
	// At v1.0.21 ValidateLogMultiConfig's second result is ctfe.LogBackendMap.
	// The declaration below is the assertion: it does not compile unless the
	// exported name exists and denotes a map keyed by backend name. A
	// successful validation must return one entry per declared backend.
	multi := &configpb.LogMultiConfig{
		Backends: &configpb.LogBackendSet{Backend: []*configpb.LogBackend{
			{Name: "bench-backend", BackendSpec: "127.0.0.1:1"},
			{Name: "other-backend", BackendSpec: "127.0.0.1:2"},
		}},
		LogConfigs: &configpb.LogConfigSet{Config: []*configpb.LogConfig{
			func() *configpb.LogConfig {
				c := cfg(false, privAny, nil, 0, 0)
				c.LogBackendName = "bench-backend"
				return c
			}(),
		}},
	}
	var backends ctfe.LogBackendMap
	backends, err = ctfe.ValidateLogMultiConfig(multi)
	t.Check("validate-log-multi-config-returns-a-log-backend-map",
		err == nil && len(backends) == 2 &&
			backends["bench-backend"].GetBackendSpec() == "127.0.0.1:1" &&
			backends["other-backend"].GetBackendSpec() == "127.0.0.1:2",
		"got %d backends, err=%v", len(backends), err)

	// The per-log rules above are reached through the whole-file validator too:
	// a log whose own config is invalid fails the multi-config.
	multi.LogConfigs.Config[0].ExtKeyUsages = []string{"HarborNotAKeyUsage"}
	badBackends, errMulti := ctfe.ValidateLogMultiConfig(multi)
	t.Check("multi-config-rejects-a-log-its-per-log-validator-refuses",
		errMulti != nil && badBackends == nil,
		"a multi-config carrying an unknown EKU was accepted")

	// Control: a rule that predates this release still fires, so "refused" is
	// not the answer to everything.
	zeroID := cfg(false, privAny, nil, 0, 0)
	zeroID.LogId = 0
	refuse("control-zero-log-id-refused", zeroID)

	t.Done()
}
