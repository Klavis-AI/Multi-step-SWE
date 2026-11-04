// Group ctfe-setup-instance.
//
// ct-spec/CLIENT-API.md §8.1-§8.2 and ct-spec/RELEASE-MAP.md §2: at v1.0.5
// `SetUpInstance` stops taking a bare (deadline, metric factory) pair and takes
// an `InstanceOptions` instead, whose three fields at this release are
// `Deadline time.Duration`, `MetricFactory monitoring.MetricFactory` and
// `ErrorMapper func(error) (int, bool)`. `TEST_INTERFACE.md` §5 calls the
// re-shaping a deliberate breaking change.
//
// SCOPE-BACKEND: nothing here dials a backend, and no socket is opened.
// `SetUpInstance` at this anchor validates the config, loads the trusted roots,
// unmarshals the private-key `Any`, builds a signer, and returns the assembled
// `*PathHandlers`; it never touches the `trillian.TrillianLogClient` it was
// handed, so a nil client reaches every set-up branch below. The last two
// cases do need a client, because they serve a request through an assembled
// handler -- they pass an in-process Go value implementing
// `trillian.TrillianLogClient` that returns a canned gRPC status without
// dialling anything.
//
// SCOPE-TIME: no clock is read for a verdict. The two serving cases go through
// `AppHandler.ServeHTTP`, which puts the configured 10 s deadline on the
// onward call; the stub answers immediately, so no case can be decided by
// timing.
// SCOPE-NET: no socket is opened.
package main

import (
	"context"
	"encoding/pem"
	"fmt"
	"net/http/httptest"
	"sort"
	"strings"

	"github.com/golang/protobuf/ptypes"
	"github.com/golang/protobuf/ptypes/any"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/trillian"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"

	_ "github.com/google/trillian/crypto/keys/der/proto"
	_ "github.com/google/trillian/crypto/keys/pem/proto"

	"ctverify/vhelp"
)

// The eight entrypoints of ct-spec/HTTP-API.md §1.1, each with the method the
// table gives it, keyed by the root package's own path constant.
var entrypoints = []struct {
	path   string
	method string
	name   string
}{
	{"/ct/v1/add-chain", "POST", "AddChain"},
	{"/ct/v1/add-pre-chain", "POST", "AddPreChain"},
	{"/ct/v1/get-sth", "GET", "GetSTH"},
	{"/ct/v1/get-sth-consistency", "GET", "GetSTHConsistency"},
	{"/ct/v1/get-proof-by-hash", "GET", "GetProofByHash"},
	{"/ct/v1/get-entries", "GET", "GetEntries"},
	{"/ct/v1/get-roots", "GET", "GetRoots"},
	{"/ct/v1/get-entry-and-proof", "GET", "GetEntryAndProof"},
}

const prefix = "bench-a"

func main() {
	t := vhelp.New()

	rootPEM := vhelp.Fixture("minted", "bench-root.pem")
	keyPEM := vhelp.Fixture("minted", "keys", "log-key.pem")
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))

	// 1. The positive path: a minimal valid LogConfig, and the option struct
	// the release requires. Eight assembled paths come back.
	handlers, err := ctfe.SetUpInstance(context.Background(), nil, validConfig(t, rootPEM, keyDER), options())
	if err != nil {
		t.Fatalf("SetUpInstance on the pinned valid config: %v", err)
	}
	if handlers == nil {
		t.Fatalf("SetUpInstance returned a nil *PathHandlers and no error")
	}
	t.Equal("the-handler-map-has-the-eight-entrypoints", len(*handlers), len(entrypoints))

	// 2. Each entry carries its documented method and EntrypointName value, so
	// the map key is the prefix plus the root package's path constant
	// (CLIENT-API.md §5, HTTP-API.md §1.1).
	for _, ep := range entrypoints {
		h, ok := (*handlers)["/"+prefix+ep.path]
		if !ok {
			t.Check("endpoint-"+ep.name+"-is-registered-at-its-path", false,
				"no handler at %q; keys %v", "/"+prefix+ep.path, sortedNames(*handlers))
			continue
		}
		t.Check("endpoint-"+ep.name+"-is-registered-at-its-path",
			string(h.Name) == ep.name && h.Method == ep.method,
			"name=%q method=%q, want %q / %q", h.Name, h.Method, ep.name, ep.method)
	}

	// 3. A prefix is normalised before the paths are assembled: a leading `/`
	// is added when absent and a trailing one removed, so `"bench-a/"` gives
	// the same eight keys. (CLIENT-API.md §5: "its keys are the assembled
	// paths".)
	slashed, err := ctfe.SetUpInstance(context.Background(), nil, configWithPrefix(t, rootPEM, keyDER, prefix+"/"), options())
	if err != nil {
		t.Check("a-trailing-slash-in-the-prefix-does-not-change-the-keys", false, "SetUpInstance: %v", err)
	} else {
		t.Equal("a-trailing-slash-in-the-prefix-does-not-change-the-keys", sortedNames(*slashed), sortedNames(*handlers))
	}

	// 4. A keyspb.PEMKeyFile is dispatched to the PEM handler that CONFIG.md
	// §2.3 says ct_server registers. What comes back is a refusal, and that is
	// expected here rather than a defect in the candidate: TEST_INTERFACE.md
	// §5.2 records that the trillian revision this benchmark pins declines to
	// open a PEM block when `password` is empty, encrypted or not, and that the
	// corpus log key is an unencrypted block named with no password. So this
	// case cannot and does not grade "a signer came out". It grades that the
	// refusal NAMES THE FILE IT WAS HANDED, which is only possible if the
	// message reached a handler that knew the path -- i.e. that `private_key`
	// went through the registry rather than being opened by the loader or
	// passed to the DER handler.
	//
	// Note for a reader comparing this with CONFIG.md §2.3's "a `password`
	// left empty means the file is not encrypted": that sentence is the
	// field's semantics and is correct; the refusal is the pinned dependency's
	// behaviour, not the field's meaning, and TEST_INTERFACE.md §5.2 states
	// both so the two pages do not disagree in front of the candidate.
	// SCOPE-DIAGNOSTIC: the wrapper texts around the path come from trillian's
	// own keys package, which no corpus page states, so they are not graded --
	// only the path, which is an identifier this group itself supplied.
	rPEM := run(t, configWithPEMKey(t, rootPEM, keyPEM))
	t.Check("a-pemfile-private-key-reaches-the-pem-handler",
		rPEM.err != nil && strings.Contains(rPEM.err.Error(), keyPEM),
		"err=%v", rPEM.err)

	// 5. The same file with a password the block does not carry is refused by
	// the same handler, one step further in: case 4 never reached decryption,
	// this one did. That is observable as a *different* refusal from case 4's,
	// without reading either one's words -- the two error strings are compared
	// only with each other, never with any pinned text (TEST_INTERFACE.md §6.1,
	// "an implementation compared with itself").
	rPW := run(t, configWithPEMKeyPassword(t, rootPEM, keyPEM, "not-the-password"))
	t.Check("a-pemfile-private-key-with-a-password-is-refused-when-unencrypted",
		rPW.err != nil && rPEM.err != nil && rPW.err.Error() != rPEM.err.Error(),
		"with-password err=%v; without err=%v", rPW.err, rPEM.err)

	// 6. `RootsPemFile` is mandatory and its absence is refused before any key
	// work. CONFIG.md §4.3 records the pre-v1.0.21 wording.
	r := run(t, &configpb.LogConfig{LogId: 1001, Prefix: prefix, PrivateKey: anyDER(t, keyDER)})
	t.Check("a-config-with-no-roots-file-is-refused",
		r.err != nil && strings.Contains(r.err.Error(), "need to specify RootsPemFile"),
		"err=%v", r.err)

	// 7. So is a missing private key, in its own words.
	r = run(t, &configpb.LogConfig{LogId: 1001, Prefix: prefix, RootsPemFile: []string{rootPEM}})
	t.Check("a-config-with-no-private-key-is-refused",
		r.err != nil && strings.Contains(r.err.Error(), "need to specify PrivateKey"),
		"err=%v", r.err)

	// 8. A private-key Any whose type has no registered handler is refused
	// rather than silently ignored. The registry's own diagnostic is trillian's
	// and is not stated by any corpus page, so only the refusal is graded.
	r = run(t, &configpb.LogConfig{
		LogId:        1001,
		Prefix:       prefix,
		RootsPemFile: []string{rootPEM},
		PrivateKey:   &any.Any{TypeUrl: "type.googleapis.com/keyspb.Specification"},
	})
	t.Check("a-private-key-of-an-unregistered-type-is-refused",
		r.err != nil, "err=%v", r.err)

	// 9. An extended key usage name outside the documented map is refused, by
	// name. CONFIG.md §4.3 dates this message to v1.0 inline.
	r = run(t, &configpb.LogConfig{
		LogId:        1001,
		Prefix:       prefix,
		RootsPemFile: []string{rootPEM},
		PrivateKey:   anyDER(t, keyDER),
		ExtKeyUsages: []string{"ServerAuth", "NotAKeyUsage"},
	})
	t.Check("an-unknown-extended-key-usage-is-refused-by-name",
		r.err != nil && strings.Contains(r.err.Error(), "unknown extended key usage: NotAKeyUsage"),
		"err=%v", r.err)

	// 10. A documented key-usage name is accepted and does not disturb the
	// handler set.
	used, err := ctfe.SetUpInstance(context.Background(), nil, func() *configpb.LogConfig {
		c := validConfig(t, rootPEM, keyDER)
		c.ExtKeyUsages = []string{"ServerAuth", "TimeStamping"}
		return c
	}(), options())
	if err != nil {
		t.Check("documented-extended-key-usages-are-accepted", false, "SetUpInstance: %v", err)
	} else {
		t.Equal("documented-extended-key-usages-are-accepted", len(*used), len(entrypoints))
	}

	// 10-11. ErrorMapper is HONOURED, not merely accepted. HTTP-API.md §6.1
	// gives the gRPC->HTTP table and then says that from v1.0.5 "the map is
	// overridable per instance by a pluggable error mapper, consulted before
	// the table". Both halves of that sentence are graded here, through an
	// assembled handler rather than through the struct:
	//
	//   * a mapper that CLAIMS the error (second return true) decides the
	//     status, overriding the table;
	//   * a mapper that DECLINES it (second return false) leaves the table in
	//     charge, so the same error comes back as the table's own answer.
	//
	// codes.NotFound is chosen because the table maps it to 404, which is
	// distinct from the mapper's 418 and from the 500 an unrecognised error
	// would give, so a build that ignores the mapper, a build that always
	// consults it, and a build that has no table at all are three different
	// observable outcomes. get-sth-consistency is the endpoint because it is
	// one of the five whose backend failure goes through the mapping at this
	// release, and `first=1` keeps it off §4.2's first=0 short circuit.
	//
	// No wording is read: only the status code.
	for _, m := range []struct {
		id     string
		claim  bool
		status int
	}{
		{id: "an-error-mapper-that-claims-the-error-overrides-the-table", claim: true, status: 418},
		{id: "an-error-mapper-that-declines-leaves-the-table-in-charge", claim: false, status: 404},
	} {
		opts := options()
		opts.ErrorMapper = func(error) (int, bool) { return 418, m.claim }
		hs, err := ctfe.SetUpInstance(context.Background(),
			failingLog{code: codes.NotFound}, validConfig(t, rootPEM, keyDER), opts)
		if err != nil {
			t.Check(m.id, false, "SetUpInstance: %v", err)
			continue
		}
		got, ok := serve(t, hs, "/"+prefix+"/ct/v1/get-sth-consistency?first=1&second=2")
		if !ok {
			t.Check(m.id, false, "no handler for the get-sth-consistency path; handlers=%v", sortedNames(*hs))
			continue
		}
		t.Equal(m.id, got, m.status)
	}

	t.Done()
}

// options is the release's InstanceOptions, spelled field for field as
// CLIENT-API.md §8.2 gives it. All three fields are set, so a candidate that
// renamed or dropped any of them does not compile this group.
func options() ctfe.InstanceOptions {
	return ctfe.InstanceOptions{
		Deadline:      10e9, // 10s, the ct_server --rpc_deadline default
		MetricFactory: monitoring.InertMetricFactory{},
		ErrorMapper:   func(error) (int, bool) { return 500, false },
	}
}

// serve runs one GET through the assembled handler for that path and returns
// the HTTP status it wrote. The second result is false when the handler map
// has no entry for the path, which is a different failure and is reported as
// one.
func serve(t *vhelp.T, hs *ctfe.PathHandlers, target string) (int, bool) {
	req := httptest.NewRequest("GET", "http://localhost"+target, nil)
	h, ok := (*hs)[req.URL.Path]
	if !ok {
		return 0, false
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	return w.Code, true
}

// failingLog is an in-process trillian.TrillianLogClient that answers every
// call with one canned gRPC status. Embedding the interface means this value
// keeps compiling as the interface grows across releases while implementing
// only the one method these cases reach; any other call would be a nil-method
// panic, which is louder than a wrong verdict.
type failingLog struct {
	trillian.TrillianLogClient
	code codes.Code
}

func (f failingLog) GetConsistencyProof(ctx context.Context, in *trillian.GetConsistencyProofRequest, opts ...grpc.CallOption) (*trillian.GetConsistencyProofResponse, error) {
	return nil, grpcstatus.Error(f.code, "canned backend failure")
}

type result struct {
	handlers *ctfe.PathHandlers
	err      error
}

// run sets up one config and reports the outcome without asserting a verdict,
// so each caller can grade its own branch.
func run(t *vhelp.T, cfg *configpb.LogConfig) result {
	h, err := ctfe.SetUpInstance(context.Background(), nil, cfg, options())
	return result{handlers: h, err: err}
}

func validConfig(t *vhelp.T, rootPEM string, keyDER []byte) *configpb.LogConfig {
	return &configpb.LogConfig{
		LogId:        1001,
		Prefix:       prefix,
		RootsPemFile: []string{rootPEM},
		PrivateKey:   anyDER(t, keyDER),
	}
}

func configWithPrefix(t *vhelp.T, rootPEM string, keyDER []byte, p string) *configpb.LogConfig {
	c := validConfig(t, rootPEM, keyDER)
	c.Prefix = p
	return c
}

// configWithPEMKey inlines the key as a keyspb.PEMKeyFile pointing at the
// pinned fixture, which is the wrapper most of this step's own configuration
// fixtures use (CONFIG.md §2.3, and the example at §5). The pinned key is stored unencrypted, so
// the password is empty.
func configWithPEMKey(t *vhelp.T, rootPEM, keyPEM string) *configpb.LogConfig {
	return configWithPEMKeyPassword(t, rootPEM, keyPEM, "")
}

func configWithPEMKeyPassword(t *vhelp.T, rootPEM, keyPEM, password string) *configpb.LogConfig {
	pb, err := ptypes.MarshalAny(&keyspb.PEMKeyFile{Path: keyPEM, Password: password})
	if err != nil {
		t.Fatalf("MarshalAny(PEMKeyFile): %v", err)
	}
	return &configpb.LogConfig{
		LogId:        1001,
		Prefix:       prefix,
		RootsPemFile: []string{rootPEM},
		PrivateKey:   pb,
	}
}

func anyDER(t *vhelp.T, keyDER []byte) *any.Any {
	pb, err := ptypes.MarshalAny(&keyspb.PrivateKey{Der: keyDER})
	if err != nil {
		t.Fatalf("MarshalAny(PrivateKey): %v", err)
	}
	return pb
}

// pemBody returns the DER inside a PEM block. A fixture that is not PEM is a
// corpus fault, not a candidate fault.
func pemBody(t *vhelp.T, b []byte) []byte {
	block, _ := pem.Decode(b)
	if block == nil {
		t.Fatalf("minted/keys/log-key.pem is not PEM")
	}
	return block.Bytes
}

func sortedNames(h map[string]ctfe.AppHandler) string {
	keys := make([]string, 0, len(h))
	for k := range h {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return fmt.Sprintf("%v", keys)
}

// x509 is referenced so the package under test is the one the step's interface
// contract names, and so a build break in it is reported here rather than
// silently skipped.
var _ = x509.ExtKeyUsageAny
