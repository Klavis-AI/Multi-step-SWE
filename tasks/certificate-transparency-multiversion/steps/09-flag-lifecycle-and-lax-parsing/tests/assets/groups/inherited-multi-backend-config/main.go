// Group inherited-multi-backend-config.
//
// The multi-backend configuration surface earlier milestones introduced:
// ctfe.MultiLogConfigFromFile parses a LogMultiConfig, ctfe.BuildLogBackendMap
// indexes its backend set, ctfe.ValidateLogMultiConfig cross-checks the log
// configs against that set, and ctfe.ToMultiLogConfig lifts a single-backend
// config into the same shape. All four are byte-identical at v1.1.0 and
// v1.1.2, so this group is an inherited regression check, not a discriminator.
//
// What v1.1.2 adds to the same file -- ValidateLogConfigs, and accepting a
// binary-encoded config -- is graded separately by config-validate-logconfigs
// and config-binary-protobuf-accepted.
//
// The shared config fixtures carry @FIXTURES@ and @BACKEND_x@ placeholders so
// they can be pointed at a live backend. Nothing here dials, so the group
// substitutes two pinned unreachable addresses: SCOPE-NET, no socket is even
// created, and the substitution is a constant rather than a chosen port.
package main

import (
	"encoding/hex"
	"os"
	"sort"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"

	"ctverify/vhelp"
)

// A pinned P-256 SubjectPublicKeyInfo. Mirror log configs need a public key
// and no private key, which makes them the cheapest config ValidateLogConfig
// will accept, so the tree-id cases below can be built in the driver rather
// than carried as fixtures.
const spkiHex = "3059301306072a8648ce3d020106082a8648ce3d03010703420004" +
	"57609453c6d4b60d3d0826690f2b80af73b8e2924f3a54df8c8b06511e5372c5" +
	"4e21bfcb2d77ff29b305a6fa4f9788138c146a969c0f3d74cb09659ad791fac9"

// mirrorOn builds a valid mirror log config bound to a named backend.
func mirrorOn(t *vhelp.T, id int64, prefix, backend string) *configpb.LogConfig {
	der, err := hex.DecodeString(spkiHex)
	if err != nil {
		t.Fatalf("bad pinned SPKI: %v", err)
	}
	return &configpb.LogConfig{
		LogId:          id,
		Prefix:         prefix,
		IsMirror:       true,
		LogBackendName: backend,
		PublicKey:      &keyspb.PublicKey{Der: der},
	}
}

// twoBackendSet is the backend half both tree-id cases share.
func twoBackendSet() *configpb.LogBackendSet {
	return &configpb.LogBackendSet{Backend: []*configpb.LogBackend{
		{Name: "backend-a", BackendSpec: backendA},
		{Name: "backend-b", BackendSpec: backendB},
	}}
}

const (
	backendA = "127.0.0.1:1"
	backendB = "127.0.0.1:2"
)

// materialise substitutes the fixture's placeholders and writes the result
// into this group's private scratch directory.
func materialise(t *vhelp.T, name string) string {
	body := string(vhelp.ReadFixture("configs", name))
	body = strings.ReplaceAll(body, "@FIXTURES@", os.Getenv("CT_FIXTURES"))
	body = strings.ReplaceAll(body, "@BACKEND_A@", backendA)
	body = strings.ReplaceAll(body, "@BACKEND_B@", backendB)
	p := os.Getenv("CT_SCRATCH") + "/" + name
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
}

// render turns a backend map into an exact, order-independent string.
func render(m ctfe.LogBackendMap) string {
	names := make([]string, 0, len(m))
	for k := range m {
		names = append(names, k)
	}
	sort.Strings(names)
	parts := make([]string, 0, len(names))
	for _, n := range names {
		parts = append(parts, n+"="+m[n].GetBackendSpec())
	}
	return strings.Join(parts, ",")
}

// prefixes renders the log prefixes of a multi config with the backend each
// one is bound to.
func prefixes(cfg *configpb.LogMultiConfig) string {
	parts := make([]string, 0)
	for _, c := range cfg.GetLogConfigs().GetConfig() {
		parts = append(parts, c.GetPrefix()+"@"+c.GetLogBackendName())
	}
	sort.Strings(parts)
	return strings.Join(parts, ",")
}

func load(t *vhelp.T, name string) *configpb.LogMultiConfig {
	cfg, err := ctfe.MultiLogConfigFromFile(materialise(t, name))
	if err != nil {
		t.Fatalf("MultiLogConfigFromFile(%s): %v", name, err)
	}
	return cfg
}

func main() {
	t := vhelp.New()

	// 1. One backend, one log: the validated map is exactly that backend.
	valid := load(t, "multi-valid.textproto")
	bm, err := ctfe.ValidateLogMultiConfig(valid)
	if err != nil {
		t.Check("single-backend-map-exact", false, "ValidateLogMultiConfig: %v", err)
	} else {
		t.Equal("single-backend-map-exact", render(bm), "default="+backendA)
	}

	// 2. Two backends, one log bound to each: both survive validation and
	// keep their own spec.
	two := load(t, "multi-two-backends.textproto")
	bm2, err := ctfe.ValidateLogMultiConfig(two)
	if err != nil {
		t.Check("two-backend-map-exact", false, "ValidateLogMultiConfig: %v", err)
	} else {
		t.Equal("two-backend-map-exact", render(bm2), "backend-a="+backendA+",backend-b="+backendB)
	}

	// 3. The log configs themselves are read back with their bindings intact.
	t.Equal("two-backend-bindings-exact", prefixes(two), "bench-a@backend-a,bench-b@backend-b")

	// 4. Two logs cannot share a URL prefix, whichever backend they sit on.
	dup := load(t, "multi-dup-prefix.textproto")
	_, err = ctfe.ValidateLogMultiConfig(dup)
	t.Check("duplicate-prefix-rejected", err != nil, "ValidateLogMultiConfig accepted a duplicate prefix")

	// 5. A log may not name a backend the set does not define.
	unknown := load(t, "multi-unknown-backend.textproto")
	_, err = ctfe.ValidateLogMultiConfig(unknown)
	t.Check("unknown-backend-rejected", err != nil, "ValidateLogMultiConfig accepted an undefined backend")

	// 6. The backend set is indexed by name, and names must be unique...
	dupName := &configpb.LogBackendSet{Backend: []*configpb.LogBackend{
		{Name: "same", BackendSpec: backendA},
		{Name: "same", BackendSpec: backendB},
	}}
	_, err = ctfe.BuildLogBackendMap(dupName)
	t.Check("duplicate-backend-name-rejected", err != nil, "BuildLogBackendMap accepted a duplicate name")

	// 7. ... and so must specs, so two names cannot alias one backend.
	dupSpec := &configpb.LogBackendSet{Backend: []*configpb.LogBackend{
		{Name: "one", BackendSpec: backendA},
		{Name: "two", BackendSpec: backendA},
	}}
	_, err = ctfe.BuildLogBackendMap(dupSpec)
	t.Check("duplicate-backend-spec-rejected", err != nil, "BuildLogBackendMap accepted a duplicate spec")

	// 8. An empty name is not a usable index key.
	empty := &configpb.LogBackendSet{Backend: []*configpb.LogBackend{{Name: "", BackendSpec: backendA}}}
	_, err = ctfe.BuildLogBackendMap(empty)
	t.Check("empty-backend-name-rejected", err != nil, "BuildLogBackendMap accepted an empty name")

	// 9. A single-backend config is lifted into the multi shape by binding
	// every log to a backend literally named "default", and the result
	// validates. The input is the log config set of the two-backend fixture,
	// whose two logs start out bound elsewhere, so the rebinding is visible.
	lifted := ctfe.ToMultiLogConfig(two.GetLogConfigs().GetConfig(), backendB)
	liftedMap, err := ctfe.ValidateLogMultiConfig(lifted)
	if err != nil {
		t.Check("single-config-lifted-to-default-backend", false, "ValidateLogMultiConfig: %v", err)
	} else {
		t.Equal("single-config-lifted-to-default-backend",
			render(liftedMap)+" "+prefixes(lifted),
			"default="+backendB+" bench-a@default,bench-b@default")
	}

	// 10. The multi-backend validator keys tree ids per backend --
	// "<backend name>-<tree id>" -- and has done since v1.0.10, so the SAME
	// tree id on two DIFFERENT backends is legal (CONFIG.md §4.4). This is the
	// positive half of the rule whose negative half the single-backend
	// ValidateLogConfigs grades globally; the two are different validators and
	// v1.1.2 changes neither this key nor this answer.
	shared := &configpb.LogMultiConfig{
		Backends: twoBackendSet(),
		LogConfigs: &configpb.LogConfigSet{Config: []*configpb.LogConfig{
			mirrorOn(t, 7, "shared-a", "backend-a"),
			mirrorOn(t, 7, "shared-b", "backend-b"),
		}},
	}
	sharedMap, err := ctfe.ValidateLogMultiConfig(shared)
	if err != nil {
		t.Check("same-tree-id-on-two-backends-accepted", false,
			"ValidateLogMultiConfig rejected tree id 7 used once per backend: %v", err)
	} else {
		t.Equal("same-tree-id-on-two-backends-accepted", render(sharedMap),
			"backend-a="+backendA+",backend-b="+backendB)
	}

	// 11. ... and the same tree id twice on ONE backend collides on that key
	// and is refused.
	collide := &configpb.LogMultiConfig{
		Backends: twoBackendSet(),
		LogConfigs: &configpb.LogConfigSet{Config: []*configpb.LogConfig{
			mirrorOn(t, 7, "collide-one", "backend-a"),
			mirrorOn(t, 7, "collide-two", "backend-a"),
		}},
	}
	_, err = ctfe.ValidateLogMultiConfig(collide)
	t.Check("same-tree-id-twice-on-one-backend-rejected", err != nil,
		"ValidateLogMultiConfig accepted tree id 7 twice on backend-a")

	t.Done()
}
