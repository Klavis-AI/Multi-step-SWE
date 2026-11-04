// Group config-multi-from-file.
//
// v1.0.5 gives the CTFE a second legal configuration shape and a loader for
// it: ctfe.MultiLogConfigFromFile parses a LogMultiConfig text proto, and is
// documented to do a partial check only -- that the file is non-empty and
// declares at least one backend and at least one log. The degenerate inputs
// below (an empty file above all, which upstream fixed inside this release)
// must come back as errors rather than a nil-dereference.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

func write(t *vhelp.T, name, body string) string {
	p := filepath.Join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
}

func materialise(t *vhelp.T, name string) string {
	body := string(vhelp.ReadFixture("configs", name))
	body = strings.ReplaceAll(body, "@FIXTURES@", vhelp.Fixture())
	body = strings.ReplaceAll(body, "@BACKEND_A@", "127.0.0.1:1")
	body = strings.ReplaceAll(body, "@BACKEND_B@", "127.0.0.1:2")
	return write(t, name, body)
}

func main() {
	t := vhelp.New()

	cfg, err := ctfe.MultiLogConfigFromFile(materialise(t, "multi-two-backends.textproto"))
	if err != nil {
		t.Check("loads-backend-set-exact", false, "MultiLogConfigFromFile: %v", err)
		t.Check("loads-log-bindings-exact", false, "MultiLogConfigFromFile: %v", err)
	} else {
		var be []string
		for _, b := range cfg.GetBackends().GetBackend() {
			be = append(be, b.GetName()+"="+b.GetBackendSpec())
		}
		t.Equal("loads-backend-set-exact", strings.Join(be, " "),
			"backend-a=127.0.0.1:1 backend-b=127.0.0.1:2")

		var logs []string
		for _, c := range cfg.GetLogConfigs().GetConfig() {
			logs = append(logs, fmt.Sprintf("%d/%s->%s", c.GetLogId(), c.GetPrefix(), c.GetLogBackendName()))
		}
		t.Equal("loads-log-bindings-exact", strings.Join(logs, " "),
			"1001/bench-a->backend-a 1002/bench-b->backend-b")
	}

	// An entirely empty file parses as an empty proto; the loader must report
	// that rather than dereference the absent sub-messages.
	_, err = ctfe.MultiLogConfigFromFile(write(t, "empty.textproto", ""))
	t.Check("empty-file-is-an-error", err != nil, "an empty file was accepted")

	// Backends but no logs.
	_, err = ctfe.MultiLogConfigFromFile(write(t, "no-logs.textproto",
		"backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"127.0.0.1:1\"\n  }\n}\n"))
	t.Check("backends-without-logs-is-an-error", err != nil, "a config with no log_configs was accepted")

	// Logs but no backends.
	_, err = ctfe.MultiLogConfigFromFile(write(t, "no-backends.textproto",
		"log_configs {\n  config {\n    log_id: 1\n    prefix: \"bench\"\n    log_backend_name: \"default\"\n  }\n}\n"))
	t.Check("logs-without-backends-is-an-error", err != nil, "a config with no backends was accepted")

	// The text-proto decoder itself refuses a field the schema does not define.
	_, err = ctfe.MultiLogConfigFromFile(write(t, "unknown.textproto",
		"backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"127.0.0.1:1\"\n    this_field_does_not_exist: true\n  }\n}\n"+
			"log_configs {\n  config {\n    log_id: 1\n    prefix: \"bench\"\n    log_backend_name: \"default\"\n  }\n}\n"))
	t.Check("unknown-field-is-an-error", err != nil, "an undefined field was accepted")

	// A path that is not there at all.
	_, err = ctfe.MultiLogConfigFromFile(filepath.Join(os.Getenv("CT_SCRATCH"), "no-such-file.textproto"))
	t.Check("missing-file-is-an-error", err != nil, "a missing file was accepted")

	t.Done()
}
