// Group config-single-to-multi.
//
// The milestone's migration promise is that the step-1 configuration file
// shape keeps working: a LogConfigSet loaded from disk plus a backend spec
// taken from --log_rpc_server is *converted* into a LogMultiConfig, so that
// from v1.0.5 on exactly one internal representation and one validator exist.
//
// ctfe.ToMultiLogConfig is that conversion. It must synthesise a single
// backend called "default" carrying the supplied spec, bind every log to it,
// leave the rest of each LogConfig untouched, and hand back something the new
// validator accepts.
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

const pinnedSpec = "127.0.0.1:65535"

func main() {
	t := vhelp.New()

	// The pinned step-1 single-backend configuration, byte for byte apart from
	// the fixture-root placeholder.
	body := strings.ReplaceAll(string(vhelp.ReadFixture("configs", "v10-single-valid.textproto")),
		"@FIXTURES@", vhelp.Fixture())
	p := filepath.Join(os.Getenv("CT_SCRATCH"), "v10-single-valid.textproto")
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}

	single, err := ctfe.LogConfigFromFile(p)
	if err != nil {
		t.Fatalf("pinned v1.0 config did not load: %v", err)
	}
	if len(single) != 1 {
		t.Fatalf("pinned v1.0 config holds %d logs, want 1", len(single))
	}
	wantRoots := single[0].GetRootsPemFile()

	multi := ctfe.ToMultiLogConfig(single, pinnedSpec)

	// 1. Exactly one synthesised backend, named "default".
	bes := multi.GetBackends().GetBackend()
	if len(bes) != 1 {
		t.Check("synthesises-one-default-backend", false, "got %d backends, want 1", len(bes))
		t.Check("default-backend-carries-spec", false, "got %d backends, want 1", len(bes))
	} else {
		t.Equal("synthesises-one-default-backend", bes[0].GetName(), "default")
		t.Equal("default-backend-carries-spec", bes[0].GetBackendSpec(), pinnedSpec)
	}

	// 2. The log is bound to it.
	cfgs := multi.GetLogConfigs().GetConfig()
	if len(cfgs) != 1 {
		t.Check("binds-log-to-default-backend", false, "got %d logs, want 1", len(cfgs))
		t.Check("preserves-original-log-fields", false, "got %d logs, want 1", len(cfgs))
	} else {
		t.Equal("binds-log-to-default-backend", cfgs[0].GetLogBackendName(), "default")
		t.Equal("preserves-original-log-fields",
			fmt.Sprintf("%d/%s/%s/%d", cfgs[0].GetLogId(), cfgs[0].GetPrefix(),
				strings.Join(cfgs[0].GetRootsPemFile(), ","), len(cfgs[0].GetPublicKey().GetDer())),
			fmt.Sprintf("1001/bench/%s/91", strings.Join(wantRoots, ",")))
	}

	// 3. The result is acceptable to the one validator.
	beMap, err := ctfe.ValidateLogMultiConfig(multi)
	if err != nil {
		t.Check("converted-config-validates", false, "ValidateLogMultiConfig: %v", err)
	} else {
		t.Check("converted-config-validates", len(beMap) == 1 && beMap["default"].GetBackendSpec() == pinnedSpec,
			"map=%v", beMap)
	}

	// 4. Several logs all land on the same synthesised backend.
	many := ctfe.ToMultiLogConfig([]*configpb.LogConfig{
		{LogId: 1, Prefix: "a"}, {LogId: 2, Prefix: "b"}, {LogId: 3, Prefix: "c"},
	}, pinnedSpec)
	var names []string
	for _, c := range many.GetLogConfigs().GetConfig() {
		names = append(names, c.GetPrefix()+"->"+c.GetLogBackendName())
	}
	t.Equal("binds-every-log-to-one-backend", strings.Join(names, " "),
		"a->default b->default c->default")

	// 5. A previously set backend name is overwritten, not honoured: the
	//    single-backend file has no way to name a backend, so the conversion
	//    is the sole authority on which backend serves these logs.
	over := ctfe.ToMultiLogConfig([]*configpb.LogConfig{
		{LogId: 9, Prefix: "z", LogBackendName: "stale"},
	}, pinnedSpec)
	t.Equal("overwrites-stale-backend-name",
		over.GetLogConfigs().GetConfig()[0].GetLogBackendName(), "default")

	t.Done()
}
