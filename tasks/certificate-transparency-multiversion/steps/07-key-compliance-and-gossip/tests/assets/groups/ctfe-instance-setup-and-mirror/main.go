// Group ctfe-instance-setup-and-mirror.
//
// v1.0.21 reduces ctfe.SetUpInstance to (ctx, opts): the Trillian client and
// the log config move inside InstanceOptions (CLIENT-API.md sections 8.1 and
// 8.2), and SetUpMirrorInstance arrives beside it for a log that serves
// another log's data. A mirror publishes no write endpoints, so the returned
// PathHandlers loses add-chain and add-pre-chain.
//
// Observed at the exported call the documentation names, with a real
// TrillianLogClient. SCOPE-BACKEND: the client speaks to this group's own
// in-process stub over a loopback gRPC socket; nothing about Trillian is
// graded, and no case below depends on anything the stub returns -- set-up
// never calls the backend.
//
// v1.0.19's SetUpInstance takes four arguments and has no
// SetUpMirrorInstance, and its InstanceOptions carries neither Config nor
// Client, so this driver does not compile against that tree.
package main

import (
	"context"
	"sort"
	"strings"
	"time"

	"github.com/golang/protobuf/ptypes"
	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/trillian"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"

	"net/http"

	"ctverify/vhelp"
)

// nowhereStorage is this group's own MirrorSTHStorage. It is never consulted
// by any case here; it exists to show SetUpMirrorInstance takes one.
type nowhereStorage struct{}

func (nowhereStorage) GetMirrorSTH(ctx context.Context, maxTreeSize int64) (*ct.SignedTreeHead, error) {
	return nil, context.Canceled
}

func logCfg(t *vhelp.T, mirror bool) *configpb.LogConfig {
	privDER, pubDER := pinnedKeyDER(t)
	cfg := &configpb.LogConfig{
		LogId:     pinnedLogID,
		Prefix:    logPrefix,
		PublicKey: &keyspb.PublicKey{Der: pubDER},
		IsMirror:  mirror,
	}
	if !mirror {
		anyKey, err := ptypes.MarshalAny(&keyspb.PrivateKey{Der: privDER})
		if err != nil {
			t.Fatalf("wrap pinned private key: %v", err)
		}
		cfg.PrivateKey = anyKey
		cfg.RootsPemFile = []string{vhelp.Fixture("minted", "roots-bench-plus-int.pem")}
	}
	return cfg
}

func opts(cfg *configpb.LogConfig, client trillian.TrillianLogClient) ctfe.InstanceOptions {
	return ctfe.InstanceOptions{
		Config:        cfg,
		Client:        client,
		Deadline:      10 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    new(ctfe.DefaultRequestLog),
	}
}

func paths(h *ctfe.PathHandlers) []string {
	out := make([]string, 0, len(*h))
	for p := range *h {
		out = append(out, p)
	}
	sort.Strings(out)
	return out
}

func main() {
	t := vhelp.New()

	_, gs, client := startBackend(t)
	defer gs.Stop()
	ctx := context.Background()

	logPaths := []string{
		"/" + logPrefix + "/ct/v1/add-chain",
		"/" + logPrefix + "/ct/v1/add-pre-chain",
		"/" + logPrefix + "/ct/v1/get-entries",
		"/" + logPrefix + "/ct/v1/get-entry-and-proof",
		"/" + logPrefix + "/ct/v1/get-proof-by-hash",
		"/" + logPrefix + "/ct/v1/get-roots",
		"/" + logPrefix + "/ct/v1/get-sth",
		"/" + logPrefix + "/ct/v1/get-sth-consistency",
	}
	mirrorPaths := []string{
		"/" + logPrefix + "/ct/v1/get-entries",
		"/" + logPrefix + "/ct/v1/get-entry-and-proof",
		"/" + logPrefix + "/ct/v1/get-proof-by-hash",
		"/" + logPrefix + "/ct/v1/get-roots",
		"/" + logPrefix + "/ct/v1/get-sth",
		"/" + logPrefix + "/ct/v1/get-sth-consistency",
	}

	// --- the two-argument form ----------------------------------------------
	o := opts(logCfg(t, false), client)
	// The five fields CLIENT-API.md section 8.2 names all live on the struct.
	o.RemoteQuotaUser = func(*http.Request) string { return "" }
	o.CertificateQuotaUser = func(*x509.Certificate) string { return "" }
	handlers, err := ctfe.SetUpInstance(ctx, o)
	if err != nil || handlers == nil {
		t.Check("setupinstance-two-arg-form", false, "SetUpInstance(ctx, opts) failed: %v", err)
		t.Check("setupinstance-log-path-set", false, "no handlers")
	} else {
		t.Check("setupinstance-two-arg-form", true, "")
		t.Equal("setupinstance-log-path-set", strings.Join(paths(handlers), " "), strings.Join(logPaths, " "))
	}

	t.Check("instanceoptions-carries-config-and-client",
		o.Config != nil && o.Client != nil && o.RequestLog != nil &&
			o.RemoteQuotaUser != nil && o.CertificateQuotaUser != nil,
		"InstanceOptions did not carry all five named fields")

	// --- prefix spelling is normalised ---------------------------------------
	for _, spelling := range []string{logPrefix, "/" + logPrefix, "/" + logPrefix + "/"} {
		cfg := logCfg(t, false)
		cfg.Prefix = spelling
		h, err := ctfe.SetUpInstance(ctx, opts(cfg, client))
		id := "setupinstance-prefix-" + strings.NewReplacer("/", "_").Replace(spelling)
		if err != nil {
			t.Check(id, false, "SetUpInstance refused prefix %q: %v", spelling, err)
			continue
		}
		t.Equal(id, strings.Join(paths(h), " "), strings.Join(logPaths, " "))
	}

	// --- a mirror publishes no write endpoints --------------------------------
	h, err := ctfe.SetUpInstance(ctx, opts(logCfg(t, true), client))
	if err != nil {
		t.Check("setupinstance-mirror-path-set", false, "SetUpInstance refused a mirror config: %v", err)
	} else {
		t.Equal("setupinstance-mirror-path-set", strings.Join(paths(h), " "), strings.Join(mirrorPaths, " "))
	}

	h, err = ctfe.SetUpMirrorInstance(ctx, opts(logCfg(t, true), client), nowhereStorage{})
	if err != nil {
		t.Check("setupmirrorinstance-path-set", false, "SetUpMirrorInstance failed: %v", err)
	} else {
		t.Equal("setupmirrorinstance-path-set", strings.Join(paths(h), " "), strings.Join(mirrorPaths, " "))
	}

	// --- refusals -------------------------------------------------------------
	refuse := func(id string, mutate func(*configpb.LogConfig)) {
		cfg := logCfg(t, false)
		mutate(cfg)
		h, err := ctfe.SetUpInstance(ctx, opts(cfg, client))
		t.Check(id, err != nil && h == nil, "set-up accepted a config it must refuse")
	}
	refuse("setupinstance-refuses-zero-log-id", func(c *configpb.LogConfig) { c.LogId = 0 })
	refuse("setupinstance-refuses-missing-roots", func(c *configpb.LogConfig) { c.RootsPemFile = nil })
	refuse("setupinstance-refuses-missing-private-key", func(c *configpb.LogConfig) { c.PrivateKey = nil })
	refuse("setupinstance-refuses-unreadable-roots", func(c *configpb.LogConfig) {
		c.RootsPemFile = []string{joinPath(vhelp.Fixture("minted"), "no-such-roots-file.pem")}
	})

	t.Done()
}
