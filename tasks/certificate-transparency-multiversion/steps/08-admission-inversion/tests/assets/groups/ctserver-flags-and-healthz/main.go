// Group ctserver-flags-and-healthz.
//
// Three changes to the ct_server command at this milestone, all observed by
// running the candidate's own binary as a subprocess or over a real socket:
//
//   - the /healthz target, which did not exist before, answers exactly "ok"
//     at the root of the HTTP endpoint whatever --handler_prefix says;
//   - --mask_internal_errors joins the flag set;
//   - --metrics_endpoint and --log_rpc_server lose their defaults, so metrics
//     are served on --http_endpoint and the backend comes from the config.
package main

import (
	"bytes"
	"net/http"
	"os/exec"
	"regexp"
	"sort"
	"strings"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

// glogFlags are contributed by the logging library, not by ct_server.
var glogFlags = map[string]bool{
	"alsologtostderr": true, "log_backtrace_at": true, "log_dir": true,
	"logtostderr": true, "stderrthreshold": true, "v": true, "vmodule": true,
}

var flagLine = regexp.MustCompile(`(?m)^\s{2}-([A-Za-z0-9_]+)`)

// helpText runs `ct_server -h` and returns its combined output. The candidate's
// output is captured, never written to this group's stdout, and the nonce is
// stripped from its environment by vhelp.Command.
func helpText(t *vhelp.T) string {
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), "-h")
	var buf bytes.Buffer
	cmd.Stdout = &buf
	cmd.Stderr = &buf
	err := cmd.Run()
	if _, ok := err.(*exec.ExitError); err != nil && !ok {
		t.Fatalf("run ct_server -h: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatalf("ct_server -h produced no output")
	}
	return buf.String()
}

// flagBlock returns the help text belonging to exactly one flag.
func flagBlock(help, name string) string {
	lines := strings.Split(help, "\n")
	for i, l := range lines {
		if strings.HasPrefix(l, "  -"+name+" ") || l == "  -"+name {
			var b strings.Builder
			b.WriteString(l)
			for _, n := range lines[i+1:] {
				if strings.HasPrefix(n, "  -") {
					break
				}
				b.WriteString("\n" + n)
			}
			return b.String()
		}
	}
	return ""
}

func main() {
	t := vhelp.New()

	help := helpText(t)

	// 1. ct_server's own flag set, exactly.
	var own []string
	for _, m := range flagLine.FindAllStringSubmatch(help, -1) {
		if !glogFlags[m[1]] {
			own = append(own, m[1])
		}
	}
	sort.Strings(own)
	want := []string{
		"by_range", "etcd_http_service", "etcd_metrics_service", "etcd_servers",
		"get_sth_interval", "handler_prefix", "http_endpoint", "log_config",
		"log_rpc_server", "mask_internal_errors", "max_get_entries",
		"metrics_endpoint", "quota_intermediate", "quota_remote", "rpc_deadline",
		"tracing", "tracing_percent", "tracing_project_id",
	}
	t.Equal("ctserver-own-flag-set", strings.Join(own, ","), strings.Join(want, ","))

	// 2. --metrics_endpoint no longer carries a default.
	mb := flagBlock(help, "metrics_endpoint")
	t.Check("metrics-endpoint-default-removed",
		mb != "" && !strings.Contains(mb, "(default "),
		"help block %q", mb)

	// 3. --log_rpc_server no longer carries a default.
	lb := flagBlock(help, "log_rpc_server")
	t.Check("log-rpc-server-default-removed",
		lb != "" && !strings.Contains(lb, "(default "),
		"help block %q", lb)

	// 4. --http_endpoint keeps its default; the two above were the only losses.
	hb := flagBlock(help, "http_endpoint")
	t.Check("http-endpoint-default-retained",
		strings.Contains(hb, `(default "localhost:6962")`), "help block %q", hb)

	// 4b. The escape hatch step 7 removed is still gone, checked by name
	//     rather than only by the exact flag set above: the binary neither
	//     advertises --allow_verification_with_non_compliant_keys nor accepts
	//     it. An unrecognised flag exits 2 (CLI.md §3). Only the status is
	//     graded -- the message on stderr is the Go flag package's own, and
	//     REQUIREMENTS.md's SCOPE-DIAGNOSTIC keeps a standard-library message
	//     permanently out of grading.
	t.Check("escape-hatch-not-advertised",
		!strings.Contains(help, "allow_verification_with_non_compliant_keys"),
		"ct_server -h still mentions the flag")

	ec := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--allow_verification_with_non_compliant_keys=false")
	var sink bytes.Buffer
	ec.Stdout = &sink
	ec.Stderr = &sink
	runErr := ec.Run()
	code := -1
	if ee, ok := runErr.(*exec.ExitError); ok {
		code = ee.ExitCode()
	} else if runErr == nil {
		code = 0
	}
	t.Check("escape-hatch-rejected-as-unknown-flag", code == 2,
		"ct_server exited %d, want 2", code)

	// 5. The personality comes up with no --log_rpc_server at all, taking its
	//    backend from the LogMultiConfig, and answers /healthz with "ok".
	h := startPersonality(t, "flags", nil, "")
	defer h.stop()
	hz := h.get(t, h.baseURL+"/healthz")
	t.Check("healthz-body-exact",
		hz.status == http.StatusOK && string(hz.body) == "ok",
		"status %d body %q", hz.status, string(hz.body))

	// 6. The root is a bare 200 and any other unknown path is a 404.
	root := h.get(t, h.baseURL+"/")
	t.Check("root-returns-empty-200",
		root.status == http.StatusOK && len(root.body) == 0,
		"status %d body %q", root.status, string(root.body))
	nf := h.get(t, h.baseURL+"/nope")
	t.Check("unknown-path-404", nf.status == http.StatusNotFound, "status %d", nf.status)

	// 7. With --metrics_endpoint unset, metrics are served on the HTTP
	//    endpoint rather than on a port of their own. Only *that* is graded:
	//    TEST_INTERFACE.md section 9 puts metrics *content* out of scope, so
	//    nothing here looks at which series the body carries.
	mt := h.get(t, h.baseURL+"/metrics")
	t.Check("metrics-on-http-endpoint",
		mt.status == http.StatusOK,
		"status %d body %d bytes", mt.status, len(mt.body))

	h.stop()

	// 8. --handler_prefix moves the log's entrypoints but not /healthz.
	p := startPersonality(t, "prefixed", []string{"--handler_prefix=/logs"}, "")
	defer p.stop()
	phz := p.get(t, p.baseURL+"/healthz")
	t.Check("healthz-ignores-handler-prefix",
		phz.status == http.StatusOK && string(phz.body) == "ok",
		"status %d body %q", phz.status, string(phz.body))
	moved := p.get(t, p.baseURL+"/logs/bench"+ct.GetRootsPath)
	stale := p.get(t, p.baseURL+"/bench"+ct.GetRootsPath)
	t.Check("handler-prefix-moves-entrypoints",
		moved.status == http.StatusOK && stale.status == http.StatusNotFound,
		"prefixed %d unprefixed %d", moved.status, stale.status)

	//    The other two unprefixed paths of HTTP-API.md §1.1 stay where they
	//    are too: the `/` catch-all is still a bare 200 at the root, and
	//    /metrics still answers at the root rather than under the prefix.
	//    Nothing here reads the metrics body -- TEST_INTERFACE.md section 9
	//    keeps metrics *content* out of scope.
	proot := p.get(t, p.baseURL+"/")
	t.Check("root-catch-all-ignores-handler-prefix",
		proot.status == http.StatusOK && len(proot.body) == 0,
		"status %d body %q", proot.status, string(proot.body))
	pmt := p.get(t, p.baseURL+"/metrics")
	t.Check("metrics-ignores-handler-prefix",
		pmt.status == http.StatusOK,
		"status %d body %d bytes", pmt.status, len(pmt.body))

	// 9. The whole mux sits inside a permissive CORS wrapper (HTTP-API.md §7),
	//    including the catch-all and /healthz.
	co := p.request(t, "GET", p.baseURL+"/healthz", map[string]string{"Origin": "https://example.invalid"})
	t.Check("cors-allow-origin-on-a-plain-get",
		co.status == http.StatusOK && co.header.Get("Access-Control-Allow-Origin") == "*" &&
			string(co.body) == "ok",
		"status %d acao %q body %q", co.status, co.header.Get("Access-Control-Allow-Origin"), string(co.body))

	pf := p.request(t, "OPTIONS", p.baseURL+"/logs/bench"+ct.AddChainPath, map[string]string{
		"Origin":                        "https://example.invalid",
		"Access-Control-Request-Method": "POST",
	})
	t.Check("cors-preflight-answered-by-the-wrapper",
		pf.status == http.StatusOK && pf.header.Get("Access-Control-Allow-Origin") == "*" &&
			pf.header.Get("Access-Control-Allow-Methods") == "POST" && len(pf.body) == 0,
		"status %d acao %q acam %q body %d bytes", pf.status,
		pf.header.Get("Access-Control-Allow-Origin"),
		pf.header.Get("Access-Control-Allow-Methods"), len(pf.body))

	t.Equal("cors-preflight-vary-lists-three-inputs",
		strings.Join(pf.header.Values("Vary"), "|"),
		"Origin|Access-Control-Request-Method|Access-Control-Request-Headers")

	t.Done()
}
