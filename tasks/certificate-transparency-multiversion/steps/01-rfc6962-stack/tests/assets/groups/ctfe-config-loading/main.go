// Group ctfe-config-loading.
//
// The CTFE's configuration surface at v1.0: LogConfigFromFile parses a
// *text*-protobuf LogConfigSet (despite --log_config's help text saying JSON),
// and SetUpInstance turns one LogConfig into a set of bound HTTP handlers.
// Both boundaries are reached with no Trillian backend: SetUpInstance is
// handed a nil client and never calls it.
//
// Config files are materialised into CT_SCRATCH with @FIXTURES@ expanded.
package main

import (
	"context"
	"encoding/pem"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"

	// The same ProtoHandler registrations ct_server performs.
	_ "github.com/google/trillian/crypto/keys/der/proto"
	_ "github.com/google/trillian/crypto/keys/pem/proto"

	"ctverify/vhelp"
)

const rpcDeadline = 10 * time.Second

var (
	ctx = context.Background()
	mf  = monitoring.InertMetricFactory{}
)

func main() {
	t := vhelp.New()

	scratch, err := ioutil.TempDir("", "cfg")
	if err != nil {
		t.Fatalf("TempDir: %v", err)
	}
	fixtures := vhelp.Fixture()

	// materialise writes body to scratch/name with @FIXTURES@ expanded.
	materialise := func(name string, body []byte) string {
		out := filepath.Join(scratch, name)
		expanded := strings.Replace(string(body), "@FIXTURES@", fixtures, -1)
		if err := ioutil.WriteFile(out, []byte(expanded), 0644); err != nil {
			t.Fatalf("write %s: %v", out, err)
		}
		return out
	}
	corpus := func(name string) string {
		b, err := ioutil.ReadFile(vhelp.Fixture("configs", name))
		if err != nil {
			t.Fatalf("read fixture configs/%s: %v", name, err)
		}
		return materialise(name, b)
	}

	// ---- LogConfigFromFile -------------------------------------------------
	cfgs, err := ctfe.LogConfigFromFile("")
	t.Check("load-empty-filename-rejected", err != nil && cfgs == nil, "n=%d err=%v", len(cfgs), err)

	cfgs, err = ctfe.LogConfigFromFile(filepath.Join(scratch, "does-not-exist.textproto"))
	t.Check("load-missing-file-rejected", err != nil && cfgs == nil, "n=%d err=%v", len(cfgs), err)

	cfgs, err = ctfe.LogConfigFromFile(materialise("empty.textproto", []byte("\n")))
	t.Check("load-empty-config-set-rejected", err != nil && len(cfgs) == 0, "n=%d err=%v", len(cfgs), err)

	cfgs, err = ctfe.LogConfigFromFile(corpus("v10-single-unknown-field.textproto"))
	t.Check("load-unknown-field-rejected", err != nil && len(cfgs) == 0, "n=%d err=%v", len(cfgs), err)

	// v1.0 reads text-proto only; the JSON rendering of the very same config
	// set is not a legal input at this release.
	cfgs, err = ctfe.LogConfigFromFile(corpus("v10-single-valid.json"))
	t.Check("load-json-encoding-rejected-at-v10", err != nil && len(cfgs) == 0, "n=%d err=%v", len(cfgs), err)

	// The missing-private-key config is *syntactically* fine: rejecting it is
	// SetUpInstance's job, not the parser's.
	cfgs, err = ctfe.LogConfigFromFile(corpus("v10-single-missing-key.textproto"))
	t.Check("load-missing-private-key-still-parses", err == nil && len(cfgs) == 1 && cfgs[0].PrivateKey == nil,
		"n=%d err=%v", len(cfgs), err)
	noKeyCfg := cfgs[0]

	cfgs, err = ctfe.LogConfigFromFile(corpus("v10-single-valid.textproto"))
	if err != nil || len(cfgs) != 1 {
		t.Fatalf("v10-single-valid.textproto: n=%d err=%v", len(cfgs), err)
	}
	t.Check("load-valid-textproto-yields-one-log", len(cfgs) == 1, "n=%d", len(cfgs))
	c := cfgs[0]

	t.Check("load-scalar-fields-exact",
		c.LogId == 1001 && c.Prefix == "bench" && c.RejectExpired == false && c.AcceptOnlyCa == false,
		"log_id=%d prefix=%q reject_expired=%v accept_only_ca=%v", c.LogId, c.Prefix, c.RejectExpired, c.AcceptOnlyCa)

	wantRoots := filepath.Join(fixtures, "minted", "roots-bench-plus-int.pem")
	t.Check("load-roots-pem-file-exact",
		len(c.RootsPemFile) == 1 && c.RootsPemFile[0] == wantRoots,
		"roots=%v", c.RootsPemFile)

	var keyFile keyspb.PEMKeyFile
	keyErr := ptypes.UnmarshalAny(c.PrivateKey, &keyFile)
	wantKey := filepath.Join(fixtures, "minted", "keys", "log-key.pem")
	t.Check("load-private-key-any-is-pemkeyfile",
		keyErr == nil && c.PrivateKey.TypeUrl == "type.googleapis.com/keyspb.PEMKeyFile" && keyFile.Path == wantKey,
		"err=%v type=%q path=%q", keyErr, c.PrivateKey.TypeUrl, keyFile.Path)

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-pubkey.pem has no PEM block")
	}
	if c.PublicKey == nil {
		t.Fatalf("parsed config has no public_key")
	}
	t.EqualBytes("load-public-key-der-exact", c.PublicKey.Der, blk.Bytes)

	t.Check("load-optional-fields-unset",
		len(c.ExtKeyUsages) == 0 && c.NotAfterStart == nil && c.NotAfterLimit == nil,
		"eku=%v start=%v limit=%v", c.ExtKeyUsages, c.NotAfterStart, c.NotAfterLimit)

	// ---- SetUpInstance -----------------------------------------------------
	tmpl, err := ioutil.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	runnable, err := ctfe.LogConfigFromFile(materialise("runnable.textproto", tmpl))
	if err != nil || len(runnable) != 1 {
		t.Fatalf("group config: n=%d err=%v", len(runnable), err)
	}
	base := runnable[0]

	handlers, err := ctfe.SetUpInstance(ctx, nil, base, rpcDeadline, mf)
	t.Check("setup-valid-config-returns-eight-handlers",
		err == nil && handlers != nil && len(*handlers) == 8,
		"err=%v handlers=%v", err, handlerCount(handlers))

	mutate := func(f func(*configpb.LogConfig)) *configpb.LogConfig {
		cp := proto.Clone(base).(*configpb.LogConfig)
		f(cp)
		return cp
	}
	refuses := func(id string, cfg *configpb.LogConfig) {
		h, err := ctfe.SetUpInstance(ctx, nil, cfg, rpcDeadline, mf)
		t.Check(id, err != nil && h == nil, "err=%v handlers=%v", err, handlerCount(h))
	}

	refuses("setup-missing-private-key-rejected", mutate(func(x *configpb.LogConfig) { x.PrivateKey = nil }))
	refuses("setup-missing-roots-rejected", mutate(func(x *configpb.LogConfig) { x.RootsPemFile = nil }))
	refuses("setup-unreadable-roots-file-rejected", mutate(func(x *configpb.LogConfig) {
		x.RootsPemFile = []string{filepath.Join(scratch, "no-such-roots.pem")}
	}))
	refuses("setup-unknown-ext-key-usage-rejected", mutate(func(x *configpb.LogConfig) {
		x.ExtKeyUsages = []string{"ServerAuth", "NoSuchUsageAtAll"}
	}))

	good := mutate(func(x *configpb.LogConfig) {
		x.ExtKeyUsages = []string{"Any", "ServerAuth", "ClientAuth", "CodeSigning", "EmailProtection",
			"IPSECEndSystem", "IPSECTunnel", "IPSECUser", "TimeStamping", "OCSPSigning",
			"MicrosoftServerGatedCrypto", "NetscapeServerGatedCrypto"}
	})
	h, err := ctfe.SetUpInstance(ctx, nil, good, rpcDeadline, mf)
	t.Check("setup-known-ext-key-usages-accepted", err == nil && h != nil && len(*h) == 8,
		"err=%v handlers=%v", err, handlerCount(h))

	// The parsed-but-keyless corpus config is refused at the same boundary.
	h, err = ctfe.SetUpInstance(ctx, nil, noKeyCfg, rpcDeadline, mf)
	t.Check("setup-refuses-corpus-config-without-private-key", err != nil && h == nil,
		"err=%v handlers=%v", err, handlerCount(h))

	os.RemoveAll(scratch)
	t.Done()
}

func handlerCount(h *ctfe.PathHandlers) int {
	if h == nil {
		return -1
	}
	return len(*h)
}
