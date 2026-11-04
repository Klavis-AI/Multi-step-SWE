// Group inherited-ctfe-log-config-validation.
//
// The CTFE configuration surface earlier milestones built, still standing at
// v1.1.4. ct_server refuses to start on a configuration it cannot validate, so
// this is the boundary that decides whether a log exists at all, and it has
// not moved since v1.1.0: at v1.1.3 and v1.1.4 trillian/ctfe/config.go differs
// only by ioutil->os and glog->klog, with no rule changed. That is what makes
// this an inherited-regression group rather than a discriminator -- it must
// pass at both anchors, and it does.
//
// Everything below goes through the exported entry points -- LogConfigFromFile,
// ValidateLogConfig, ValidateLogConfigs -- and asserts either an accept/reject
// verdict or an exact value the validator publishes in ValidatedLogConfig. No
// error string is graded. The starting point is the shared pinned config
// corpus, whose @FIXTURES@ placeholder is substituted into this group's own
// scratch directory; nothing is written into the candidate's tree and no
// socket is created (SCOPE-NET), because validation never dials a backend.
package main

import (
	"crypto/ecdsa"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/x509"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/timestamppb"

	"ctverify/vhelp"
)

// materialise substitutes the fixture corpus's placeholders and writes the
// result into this group's private scratch directory.
func materialise(t *vhelp.T, name string) string {
	body := strings.ReplaceAll(string(vhelp.ReadFixture("configs", name)), "@FIXTURES@", os.Getenv("CT_FIXTURES"))
	p := os.Getenv("CT_SCRATCH") + "/" + name
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
}

// clone copies a config so each case mutates an independent value.
func clone(c *configpb.LogConfig) *configpb.LogConfig {
	return proto.Clone(c).(*configpb.LogConfig)
}

func main() {
	t := vhelp.New()

	// --- the pinned single-log configuration ------------------------------
	cfgs, err := ctfe.LogConfigFromFile(materialise(t, "v10-single-valid.textproto"))
	if err != nil {
		t.Fatalf("LogConfigFromFile(v10-single-valid.textproto): %v", err)
	}
	if len(cfgs) != 1 {
		t.Fatalf("pinned config holds %d logs, want 1", len(cfgs))
	}
	base := cfgs[0]
	t.Equal("single-log-config-loads-exactly",
		fmt.Sprintf("%d/%s/%d/%s", len(cfgs), base.GetPrefix(), base.GetLogId(), base.GetRootsPemFile()[0]),
		"1/bench/1001/"+os.Getenv("CT_FIXTURES")+"/minted/roots-bench-plus-int.pem")

	// The validator republishes the log's public key. Its SHA-256 over the DER
	// SubjectPublicKeyInfo is the log id every SCT in this corpus carries, so
	// this one case ties the configuration surface to the signing surface.
	v, err := ctfe.ValidateLogConfig(base)
	if err != nil {
		t.Fatalf("ValidateLogConfig(pinned valid config): %v", err)
	}
	pub, ok := v.PubKey.(*ecdsa.PublicKey)
	if !ok || pub.Curve == nil {
		t.Check("validated-public-key-is-the-configured-p256-key", false, "PubKey is %T", v.PubKey)
	} else {
		der, merr := x509.MarshalPKIXPublicKey(pub)
		if merr != nil {
			t.Check("validated-public-key-is-the-configured-p256-key", false, "marshal: %v", merr)
		} else {
			sum := sha256.Sum256(der)
			want := strings.SplitN(strings.TrimSpace(string(vhelp.ReadFixture("minted", "keys", "log-id.txt"))), "\n", 2)[0]
			t.Equal("validated-public-key-is-the-configured-p256-key",
				pub.Curve.Params().Name+"/"+hex.EncodeToString(sum[:]), "P-256/"+want)
		}
	}
	t.Check("validated-private-key-is-published", v.PrivKey != nil, "PrivKey is nil for a non-mirror log")
	t.Check("unset-optional-fields-stay-unset",
		v.NotAfterStart == nil && v.NotAfterLimit == nil && v.KeyUsages == nil && v.FrozenSTH == nil,
		"start=%v limit=%v kus=%v sth=%v", v.NotAfterStart, v.NotAfterLimit, v.KeyUsages, v.FrozenSTH)

	// --- the two pinned reject fixtures -----------------------------------
	// A log with no private key cannot sign, and is refused at validation.
	nokey, err := ctfe.LogConfigFromFile(materialise(t, "v10-single-missing-key.textproto"))
	if err != nil {
		t.Check("config-without-a-private-key-is-refused", false, "LogConfigFromFile: %v", err)
	} else {
		_, verr := ctfe.ValidateLogConfig(nokey[0])
		t.Check("config-without-a-private-key-is-refused", verr != nil,
			"ValidateLogConfig accepted a log config with no private key")
	}

	// An unknown field is refused by the decoder, before any rule runs.
	_, err = ctfe.LogConfigFromFile(materialise(t, "v10-single-unknown-field.textproto"))
	t.Check("unknown-config-field-is-refused-by-the-parser", err != nil,
		"LogConfigFromFile accepted a config carrying an unknown field")

	// A file that is not a configuration at all is not silently empty.
	junk := os.Getenv("CT_SCRATCH") + "/not-a-config.textproto"
	if werr := os.WriteFile(junk, []byte("this is not a protobuf\x00\x01\x02"), 0600); werr != nil {
		t.Fatalf("write junk: %v", werr)
	}
	_, err = ctfe.LogConfigFromFile(junk)
	t.Check("unparseable-config-file-is-refused", err != nil, "LogConfigFromFile accepted junk")

	// --- the single-log rules ---------------------------------------------
	noID := clone(base)
	noID.LogId = 0
	_, err = ctfe.ValidateLogConfig(noID)
	t.Check("log-without-a-tree-id-is-refused", err != nil, "ValidateLogConfig accepted log_id 0")

	rejectAll := clone(base)
	rejectAll.RejectExpired, rejectAll.RejectUnexpired = true, true
	_, err = ctfe.ValidateLogConfig(rejectAll)
	t.Check("a-log-that-rejects-everything-is-refused", err != nil,
		"ValidateLogConfig accepted reject_expired and reject_unexpired together")

	// A known extended key usage name is translated to the exact x509 value;
	// an unknown one is refused rather than ignored.
	eku := clone(base)
	eku.ExtKeyUsages = []string{"ServerAuth", "ClientAuth"}
	vEKU, err := ctfe.ValidateLogConfig(eku)
	if err != nil {
		t.Check("ext-key-usage-names-map-to-exact-x509-values", false, "ValidateLogConfig: %v", err)
	} else {
		t.Equal("ext-key-usage-names-map-to-exact-x509-values", fmt.Sprint(vEKU.KeyUsages),
			fmt.Sprint([]x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth, x509.ExtKeyUsageClientAuth}))
	}
	badEKU := clone(base)
	badEKU.ExtKeyUsages = []string{"ServerAuth", "NoSuchUsage"}
	_, err = ctfe.ValidateLogConfig(badEKU)
	t.Check("unknown-ext-key-usage-is-refused", err != nil, "ValidateLogConfig accepted an unknown EKU name")

	// The NotAfter window is carried through as exact UTC times, and an
	// inverted window is refused.
	window := clone(base)
	start := time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	limit := time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	window.NotAfterStart, window.NotAfterLimit = timestamppb.New(start), timestamppb.New(limit)
	vWin, err := ctfe.ValidateLogConfig(window)
	if err != nil {
		t.Check("not-after-window-is-carried-through-exactly", false, "ValidateLogConfig: %v", err)
	} else if vWin.NotAfterStart == nil || vWin.NotAfterLimit == nil {
		t.Check("not-after-window-is-carried-through-exactly", false, "window not published")
	} else {
		t.Equal("not-after-window-is-carried-through-exactly",
			vWin.NotAfterStart.UTC().Format(time.RFC3339)+".."+vWin.NotAfterLimit.UTC().Format(time.RFC3339),
			"2020-01-01T00:00:00Z..2030-01-01T00:00:00Z")
	}
	inverted := clone(base)
	inverted.NotAfterStart, inverted.NotAfterLimit = timestamppb.New(limit), timestamppb.New(start)
	_, err = ctfe.ValidateLogConfig(inverted)
	t.Check("inverted-not-after-window-is-refused", err != nil, "ValidateLogConfig accepted limit before start")

	// Merge delays: negative is meaningless, and the expected delay may not
	// exceed the maximum.
	mmd := clone(base)
	mmd.MaxMergeDelaySec, mmd.ExpectedMergeDelaySec = 86400, 7200
	_, err = ctfe.ValidateLogConfig(mmd)
	okMMD := err == nil
	bad := clone(base)
	bad.MaxMergeDelaySec, bad.ExpectedMergeDelaySec = 7200, 86400
	_, errBad := ctfe.ValidateLogConfig(bad)
	neg := clone(base)
	neg.MaxMergeDelaySec = -1
	_, errNeg := ctfe.ValidateLogConfig(neg)
	t.Check("merge-delays-must-be-non-negative-and-ordered",
		okMMD && errBad != nil && errNeg != nil,
		"ordered=%v exceeds-mmd-err=%v negative-err=%v", okMMD, errBad, errNeg)

	// A mirror log verifies, it does not sign: it needs a public key and must
	// not carry a private one.
	mirror := clone(base)
	mirror.IsMirror, mirror.PrivateKey = true, nil
	_, errMirror := ctfe.ValidateLogConfig(mirror)
	mirrorNoPub := clone(mirror)
	mirrorNoPub.PublicKey = nil
	_, errMirrorNoPub := ctfe.ValidateLogConfig(mirrorNoPub)
	mirrorPriv := clone(base)
	mirrorPriv.IsMirror = true
	_, errMirrorPriv := ctfe.ValidateLogConfig(mirrorPriv)
	t.Check("a-mirror-needs-a-public-key-and-no-private-key",
		errMirror == nil && errMirrorNoPub != nil && errMirrorPriv != nil,
		"mirror=%v no-pubkey=%v with-privkey=%v", errMirror, errMirrorNoPub, errMirrorPriv)

	// --- the rules over a whole config set ---------------------------------
	second := clone(base)
	second.LogId, second.Prefix = 1002, "bench-two"
	if err := ctfe.ValidateLogConfigs([]*configpb.LogConfig{base, second}); err != nil {
		t.Check("two-distinct-logs-validate-together", false, "ValidateLogConfigs: %v", err)
	} else {
		t.Check("two-distinct-logs-validate-together", true, "")
	}

	dupPrefix := clone(second)
	dupPrefix.Prefix = base.GetPrefix()
	errPrefix := ctfe.ValidateLogConfigs([]*configpb.LogConfig{base, dupPrefix})
	dupTree := clone(second)
	dupTree.LogId = base.GetLogId()
	errTree := ctfe.ValidateLogConfigs([]*configpb.LogConfig{base, dupTree})
	noPrefix := clone(second)
	noPrefix.Prefix = ""
	errEmpty := ctfe.ValidateLogConfigs([]*configpb.LogConfig{base, noPrefix})
	t.Check("a-config-set-refuses-duplicate-prefixes-tree-ids-and-empty-prefixes",
		errPrefix != nil && errTree != nil && errEmpty != nil,
		"dup-prefix=%v dup-tree-id=%v empty-prefix=%v", errPrefix, errTree, errEmpty)

	t.Done()
}
