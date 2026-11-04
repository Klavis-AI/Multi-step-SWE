// config-duplicate-prefix observes the log-prefix uniqueness rule that
// ValidateLogMultiConfig gains at v1.0.10 (rule 5 of its own doc comment:
// "The prefixes of configured logs must all be distinct and must not be
// empty").  v1.0.5's validator only checked backend names, backend specs and
// that every log names a defined backend, so it accepts two logs that share a
// prefix; the two would only collide later, when both tried to register the
// same HTTP path.
//
// The boundary is the exported library function the release documents.  The
// canonical accept/reject pair comes from the pinned corpus together with its
// own .expect verdict file, so the verdict is not this driver's opinion.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

// materialise copies a pinned config fixture into the group's private scratch
// directory with the corpus placeholders replaced.  Nothing here is a port: the
// backend specs are opaque strings to the validator, and the corpus documents
// that config validation only requires backend_spec to be non-empty.
func materialise(t *vhelp.T, name string) string {
	body := string(vhelp.ReadFixture("configs", name))
	body = strings.ReplaceAll(body, "@FIXTURES@", os.Getenv("CT_FIXTURES"))
	body = strings.ReplaceAll(body, "@BACKEND_A@", "127.0.0.1:1")
	body = strings.ReplaceAll(body, "@BACKEND_B@", "127.0.0.1:2")
	p := filepath.Join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", name, err)
	}
	return p
}

// wantFromCorpus reads the pinned single-word verdict that ships beside the
// fixture.  An unexpected word is a corpus fault and aborts the group.
func wantFromCorpus(t *vhelp.T, name string) bool {
	w := strings.TrimSpace(string(vhelp.ReadFixture("configs", name+".expect")))
	switch w {
	case "accept":
		return true
	case "reject":
		return false
	}
	t.Fatalf("fixture %s.expect holds %q, want accept or reject", name, w)
	return false
}

func backend(name, spec string) *configpb.LogBackend {
	return &configpb.LogBackend{Name: name, BackendSpec: spec}
}

func logCfg(id int64, prefix, backendName string) *configpb.LogConfig {
	return &configpb.LogConfig{LogId: id, Prefix: prefix, LogBackendName: backendName}
}

func multi(backends []*configpb.LogBackend, logs []*configpb.LogConfig) *configpb.LogMultiConfig {
	return &configpb.LogMultiConfig{
		Backends:   &configpb.LogBackendSet{Backend: backends},
		LogConfigs: &configpb.LogConfigSet{Config: logs},
	}
}

func main() {
	t := vhelp.New()

	// --- the pinned corpus pair, verdicts taken from the .expect files -------
	for _, name := range []string{"multi-valid.textproto", "multi-dup-prefix.textproto"} {
		want := wantFromCorpus(t, name)
		cfg, err := ctfe.MultiLogConfigFromFile(materialise(t, name))
		if err != nil {
			t.Check("corpus-"+name, false, "MultiLogConfigFromFile: %v", err)
			continue
		}
		_, err = ctfe.ValidateLogMultiConfig(cfg)
		got := err == nil
		t.Check("corpus-"+name, got == want,
			"ValidateLogMultiConfig accepted=%v, corpus says accept=%v (err=%v)", got, want, err)
	}

	// --- the rule, stated three ways ----------------------------------------
	cases := []struct {
		id     string
		cfg    *configpb.LogMultiConfig
		accept bool
	}{
		{
			// Two logs, one backend, distinct prefixes: the shape v1.0.10 keeps.
			id: "distinct-prefixes-one-backend",
			cfg: multi(
				[]*configpb.LogBackend{backend("only", "spec-a")},
				[]*configpb.LogConfig{logCfg(1001, "bench-a", "only"), logCfg(1002, "bench-b", "only")}),
			accept: true,
		},
		{
			// The same prefix twice on the same backend.
			id: "same-prefix-one-backend",
			cfg: multi(
				[]*configpb.LogBackend{backend("only", "spec-a")},
				[]*configpb.LogConfig{logCfg(1001, "bench", "only"), logCfg(1002, "bench", "only")}),
			accept: false,
		},
		{
			// Prefix uniqueness is global, not per backend: an HTTP path is
			// served by one process however many backends stand behind it.
			id: "same-prefix-two-backends",
			cfg: multi(
				[]*configpb.LogBackend{backend("a", "spec-a"), backend("b", "spec-b")},
				[]*configpb.LogConfig{logCfg(1001, "bench", "a"), logCfg(1002, "bench", "b")}),
			accept: false,
		},
	}
	for _, c := range cases {
		_, err := ctfe.ValidateLogMultiConfig(c.cfg)
		got := err == nil
		t.Check(c.id, got == c.accept, "accepted=%v, want accepted=%v (err=%v)", got, c.accept, err)
	}

	// --- the two rejections that live in the loaders, not the validator ----
	// CONFIG.md 4.4 rows 1 and 2 are the only two whole-file rules that
	// ValidateLogMultiConfig cannot reach: they are enforced by
	// LogConfigFromFile and MultiLogConfigFromFile as they read. Both are
	// v1.0.5 rules; the group as a whole still fails at v1.0.5 on its prefix
	// cases. The two files are written here rather than pinned in the corpus
	// because their whole content is "nothing" and "one half", and a fixture
	// would say no more than the literal does.
	writeScratch := func(name, body string) string {
		q := filepath.Join(os.Getenv("CT_SCRATCH"), name)
		if err := os.WriteFile(q, []byte(body), 0600); err != nil {
			t.Fatalf("write %s: %v", name, err)
		}
		return q
	}

	_, errEmptyFile := ctfe.LogConfigFromFile(writeScratch("empty-log-config.textproto", ""))
	t.Check("empty-log-config-file-rejected", errEmptyFile != nil,
		"LogConfigFromFile accepted a file holding no log config")

	backendsOnly := "backends {\n  backend {\n    name: \"only\"\n    backend_spec: \"127.0.0.1:1\"\n  }\n}\n"
	_, errHalf := ctfe.MultiLogConfigFromFile(writeScratch("multi-backends-only.textproto", backendsOnly))
	t.Check("multi-config-missing-log-configs-rejected", errHalf != nil,
		"MultiLogConfigFromFile accepted a multi-config with no log_configs")

	// --- step 02's conversion, still reachable ------------------------------
	// ToMultiLogConfig is the single-backend-to-multi conversion step 02
	// established. Nothing else in this step calls it, and Harbor never
	// re-runs step 02's verifier, so this is where it stays covered. It names
	// the synthesised backend "default" and rewrites every log to point at it.
	converted := ctfe.ToMultiLogConfig(
		[]*configpb.LogConfig{logCfg(2001, "conv-a", ""), logCfg(2002, "conv-b", "")},
		"127.0.0.1:9100")
	shape := fmt.Sprintf("%d/%s/%s/%s/%s",
		len(converted.GetBackends().GetBackend()),
		converted.GetBackends().GetBackend()[0].GetName(),
		converted.GetBackends().GetBackend()[0].GetBackendSpec(),
		converted.GetLogConfigs().GetConfig()[0].GetLogBackendName(),
		converted.GetLogConfigs().GetConfig()[1].GetLogBackendName())
	t.Equal("to-multi-log-config-shape", shape, "1/default/127.0.0.1:9100/default/default")

	_, errConv := ctfe.ValidateLogMultiConfig(converted)
	t.Check("to-multi-log-config-output-validates", errConv == nil,
		"ValidateLogMultiConfig rejected a converted single-backend config: %v", errConv)

	// The accepted config must also hand back the backend map the caller uses
	// to dial: one entry per declared backend, keyed by name.
	beMap, err := ctfe.ValidateLogMultiConfig(cases[0].cfg)
	if err != nil {
		t.Check("backend-map", false, "unexpected error: %v", err)
	} else {
		got := fmt.Sprintf("%d/%v", len(beMap), beMap["only"].GetBackendSpec())
		t.Equal("backend-map", got, "1/spec-a")
	}

	t.Done()
}
