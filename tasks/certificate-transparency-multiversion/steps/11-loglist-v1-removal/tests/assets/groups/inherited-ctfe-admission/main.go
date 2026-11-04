// Group inherited-ctfe-admission.
//
// What the CTFE will and will not admit, inherited and not re-graded anywhere
// else in this step. Two surfaces, both older than v1.1.4 and neither touched
// by it:
//
//   - serving mode. `is_mirror` (v1.0.21) and `is_readonly` (v1.1.3) both
//     un-register the two write endpoints, so add-chain and add-pre-chain are
//     404 rather than 405 or 403 -- HTTP-API.md section 6.2, CONFIG.md
//     section 2.5. `is_readonly` is step 10's milestone; this group is what
//     stops step 11 quietly dropping it.
//   - the certificate quota user (v1.0.19), CLIENT-API.md section 8.6:
//     a pure, total function of a certificate whose exact three-field format
//     that section pins.
//
// trillian/ctfe/handlers.go and trillian/ctfe/cert_quota.go differ at v1.1.3
// and v1.1.4 only by glog->klog and ioutil->io, with no rule changed, so every
// case below must pass at both anchors and none of them is satisfiable by this
// step's new capability.
//
// The endpoint set is asserted against path literals written out here from
// HTTP-API.md, not read back from the product's own constants, and the quota
// identity is recomputed here from the fixture's DER. SCOPE-NET: nothing is
// dialled and no socket is created -- SetUpInstance performs no RPC, so a nil
// Trillian client is enough to build an instance and ask it which endpoints it
// registered. Nothing is written into the candidate's tree; the one file this
// group writes is the placeholder-substituted config in its own scratch
// directory.
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"os"
	"sort"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/trillian/crypto/keys"
	keysder "github.com/google/trillian/crypto/keys/der"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

// The eight endpoints a writable log registers, spelled out from
// HTTP-API.md rather than taken from the product's own constants, under the
// pinned config's "bench" prefix.
const prefix = "/bench"

var (
	readPaths = []string{
		prefix + "/ct/v1/get-entries",
		prefix + "/ct/v1/get-entry-and-proof",
		prefix + "/ct/v1/get-proof-by-hash",
		prefix + "/ct/v1/get-roots",
		prefix + "/ct/v1/get-sth",
		prefix + "/ct/v1/get-sth-consistency",
	}
	writePaths = []string{
		prefix + "/ct/v1/add-chain",
		prefix + "/ct/v1/add-pre-chain",
	}
)

// The worked example CLIENT-API.md section 8.6 gives for the subject field:
// RFC 2253 order, comma-separated, most specific first.
const fakeIntermediateSubject = "CN=FakeIntermediateAuthority,OU=Eng,O=Google,L=London,ST=London,C=GB"

func materialise(t *vhelp.T, name string) string {
	body := strings.ReplaceAll(string(vhelp.ReadFixture("configs", name)), "@FIXTURES@", os.Getenv("CT_FIXTURES"))
	p := os.Getenv("CT_SCRATCH") + "/" + name
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
}

func clone(c *configpb.LogConfig) *configpb.LogConfig {
	return proto.Clone(c).(*configpb.LogConfig)
}

// paths returns the sorted endpoint set an instance built from cfg registers,
// or the error that stopped it being built.
func paths(cfg *configpb.LogConfig) ([]string, error) {
	v, err := ctfe.ValidateLogConfig(cfg)
	if err != nil {
		return nil, err
	}
	inst, err := ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:     v,
		Deadline:      time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    new(ctfe.DefaultRequestLog),
	})
	if err != nil {
		return nil, err
	}
	got := make([]string, 0, len(inst.Handlers))
	for p := range inst.Handlers {
		got = append(got, p)
	}
	sort.Strings(got)
	return got, nil
}

func union(a, b []string) []string {
	out := append(append([]string{}, a...), b...)
	sort.Strings(out)
	return out
}

func show(v []string, err error) string {
	if err != nil {
		return "error: " + err.Error()
	}
	return strings.Join(v, " ")
}

// pemBody returns the DER inside a single-block PEM file.
func pemBody(t *vhelp.T, raw []byte) []byte {
	blk, _ := pem.Decode(raw)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

// readCert loads a PEM or DER certificate from the fixture corpus.
func readCert(t *vhelp.T, rel ...string) *x509.Certificate {
	raw := vhelp.ReadFixture(rel...)
	if blk, _ := pem.Decode(raw); blk != nil {
		raw = blk.Bytes
	}
	c, err := x509.ParseCertificate(raw)
	if x509.IsFatal(err) {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return c
}

// wantQuotaUser recomputes CLIENT-API.md section 8.6's format independently of
// the product: the exported prefix, the RFC 2253 subject, and the first five
// bytes of SHA-256 over the raw DER SubjectPublicKeyInfo as ten lower-case hex
// characters.
func wantQuotaUser(c *x509.Certificate) string {
	h := sha256.Sum256(c.RawSubjectPublicKeyInfo)
	return fmt.Sprintf("@intermediate %s %s", c.Subject.String(), hex.EncodeToString(h[0:5]))
}

func main() {
	t := vhelp.New()

	// Loading a private key needs its handler registered; ct_server's own
	// init() registers three, and this group stands in for that main. Nothing
	// about the registry is graded here -- the key only has to load, so that
	// a failure below is a statement about serving mode and not about key
	// plumbing.
	keys.RegisterHandler(&keyspb.PrivateKey{}, keysder.FromProto)

	// --- serving mode ------------------------------------------------------
	cfgs, err := ctfe.LogConfigFromFile(materialise(t, "v10-single-valid.textproto"))
	if err != nil || len(cfgs) != 1 {
		t.Fatalf("LogConfigFromFile(v10-single-valid.textproto): %d configs, %v", len(cfgs), err)
	}
	base := cfgs[0]

	// The pinned corpus stores the log key as an unencrypted PKCS#8 PEM, and
	// trillian's PEMKeyFile handler insists on a password, so the key is
	// carried inline as DER -- the same substitution the shared server kit
	// makes for the same fixture.
	anyKey, err := anypb.New(&keyspb.PrivateKey{Der: pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))})
	if err != nil {
		t.Fatalf("wrap log key: %v", err)
	}
	base.PrivateKey = anyKey

	// A plain writable log registers all eight. The control: without it the
	// two removals below could be satisfied by a log that registers nothing.
	plain, errPlain := paths(base)
	t.Equal("a-writable-log-registers-all-eight-endpoints",
		show(plain, errPlain), strings.Join(union(readPaths, writePaths), " "))

	// is_readonly removes exactly the two write paths and leaves the other six
	// untouched -- not 405 on them, not present at all.
	ro := clone(base)
	ro.IsReadonly = true
	roPaths, errRO := paths(ro)
	t.Equal("is-readonly-removes-exactly-the-two-write-endpoints",
		show(roPaths, errRO), strings.Join(readPaths, " "))

	// is_mirror, older by one release, removes the same two. A mirror may not
	// carry a private key, so that field goes with the flag.
	mirror := clone(base)
	mirror.IsMirror, mirror.PrivateKey = true, nil
	mirrorPaths, errMirror := paths(mirror)
	t.Equal("is-mirror-removes-the-same-two-endpoints",
		show(mirrorPaths, errMirror), strings.Join(readPaths, " "))

	// "the log keeps its key": a read-only log is still a real log, so it
	// still needs its roots and its signer, and fails to start without them.
	// A mirror, which signs nothing, does not. This is what distinguishes the
	// two modes underneath an endpoint set they share.
	roNoRoots := clone(ro)
	roNoRoots.RootsPemFile = nil
	_, errRONoRoots := paths(roNoRoots)
	roBadKey := clone(ro)
	roBadKey.PrivateKey = nil
	_, errRONoKey := paths(roBadKey)
	mirrorNoRoots := clone(mirror)
	mirrorNoRoots.RootsPemFile = nil
	_, errMirrorNoRoots := paths(mirrorNoRoots)
	t.Check("a-readonly-log-still-needs-roots-and-a-key-a-mirror-needs-neither",
		errRONoRoots != nil && errRONoKey != nil && errMirrorNoRoots == nil,
		"readonly-no-roots=%v readonly-no-key=%v mirror-no-roots=%v",
		errRONoRoots, errRONoKey, errMirrorNoRoots)

	// --- the certificate quota user ----------------------------------------
	t.Equal("quota-user-prefix-is-the-exported-constant",
		ctfe.CertificateQuotaUserPrefix, "@intermediate")

	// The worked example from CLIENT-API.md section 8.6, pinned whole: the
	// subject renders in RFC 2253 order and the tail is ten hex characters.
	intCA := readCert(t, "keys", "int-ca.cert")
	gotInt := ctfe.QuotaUserForCert(intCA)
	spki := sha256.Sum256(intCA.RawSubjectPublicKeyInfo)
	t.Equal("quota-user-for-an-intermediate-is-prefix-subject-and-spki-hash",
		gotInt, "@intermediate "+fakeIntermediateSubject+" "+hex.EncodeToString(spki[0:5]))

	// Total over a root, an intermediate and a leaf -- it "validates nothing
	// and rejects nothing" -- and a function of the key, so three certificates
	// with three different keys get three different identities.
	root := readCert(t, "keys", "fake-ca.cert")
	leaf := readCert(t, "minted", "leaf-valid.der")
	gotRoot, gotLeaf := ctfe.QuotaUserForCert(root), ctfe.QuotaUserForCert(leaf)
	distinct := gotRoot != gotInt && gotInt != gotLeaf && gotRoot != gotLeaf
	t.Check("quota-user-is-total-over-a-root-an-intermediate-and-a-leaf",
		gotRoot == wantQuotaUser(root) && gotLeaf == wantQuotaUser(leaf) && distinct &&
			gotRoot == ctfe.QuotaUserForCert(root),
		"root=%q leaf=%q distinct=%v", gotRoot, gotLeaf, distinct)

	t.Done()
}
