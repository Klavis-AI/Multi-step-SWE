// Group config-multi-validation.
//
// v1.0.5 introduces the single validator every CTFE configuration now passes
// through: ctfe.ValidateLogMultiConfig. It returns the name->backend map the
// server later uses to pick an RPC client, and it refuses four distinct
// malformed shapes:
//
//	an empty backend name, an empty backend_spec, a duplicated backend name,
//	and a log whose log_backend_name names a backend that was never declared.
//
// The accept cases are driven from the pinned corpus fixtures; the reject
// cases are built in-driver so each one isolates exactly one defect.
//
// The library verdict is only half the contract. CLI.md section 2.2 puts this
// same validator on ct_server's start-up path as step 2 of eight and requires
// an invalid configuration to be reported as "Invalid config: ..." and to exit
// 1 "before any socket is touched". A candidate can satisfy every library case
// below and still serve a configuration the validator rejected, so the last
// two cases feed the two malformed shapes the documents name to the
// candidate's own binary and require it to refuse them.
package main

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

// materialise copies a pinned config fixture into the group's scratch
// directory with its placeholders replaced. Validation only requires
// backend_spec to be a non-empty string, so the substitutes are literals.
func materialise(t *vhelp.T, name string) string {
	body := string(vhelp.ReadFixture("configs", name))
	body = strings.ReplaceAll(body, "@FIXTURES@", vhelp.Fixture())
	body = strings.ReplaceAll(body, "@BACKEND_A@", "127.0.0.1:1")
	body = strings.ReplaceAll(body, "@BACKEND_B@", "127.0.0.1:2")
	p := filepath.Join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
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

// --- process level ---------------------------------------------------------

// cmvPort returns a free loopback port. Same policy as the corpus's shared
// server kit: ports are taken from BELOW the kernel's ephemeral range so the
// kernel cannot hand the same port to an unrelated process in the window
// between this probe closing and the child binding it, and the starting slot
// is dispersed by one throwaway ephemeral bind so sibling probes in a
// concurrent run do not converge. No fixed port number appears anywhere.
func cmvPort(t *vhelp.T) int {
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close probe listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		p := 20000 + (base+i)%12000
		l, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", p))
		if err != nil {
			continue
		}
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return p
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return 0
}

// refused runs the candidate's own ct_server on cfgPath and reports whether it
// refused the configuration: exit status 1 with "Invalid config:" on stderr,
// the two observables CLI.md section 2.2 fixes for this failure.
//
// The graded event is the process exit, not how long it took. The deadline
// below is a safety net for the failing direction only -- a server that
// wrongly ACCEPTS the configuration starts serving and never exits, and the
// group must report that rather than hang -- so no passing run depends on it.
// Both endpoints are explicit free loopback ports rather than the compiled-in
// defaults, so a candidate that wrongly accepts cannot collide with anything.
func refused(t *vhelp.T, tag, cfgPath string) (bool, string) {
	// Exactly the command line TEST_INTERFACE section 4.1 fixes for every
	// group that drives the front end, with the multi-backend row's single
	// appended flag. Nothing extra is passed and no positional is.
	args := []string{
		"--log_config=" + cfgPath,
		"--http_endpoint=127.0.0.1:" + strconv.Itoa(cmvPort(t)),
		"--metrics_endpoint=127.0.0.1:" + strconv.Itoa(cmvPort(t)),
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr",
		"--log_rpc_server=",
	}
	errPath := filepath.Join(os.Getenv("CT_SCRATCH"), "ct_server-"+tag+".stderr")
	sink, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	// vhelp.Command strips CT_VERIFY_NONCE from the child's environment and
	// the child's output goes to this file rather than to the group's own
	// stdout, so nothing the candidate prints can be mistaken for a case line.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), args...)
	cmd.Stdout = sink
	cmd.Stderr = sink
	if err := cmd.Start(); err != nil {
		_ = sink.Close()
		t.Fatalf("start ct_server: %v", err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()

	code := -1
	timedOut := false
	select {
	case werr := <-done:
		code = 0
		if ee, ok := werr.(*exec.ExitError); ok {
			code = ee.ExitCode()
		} else if werr != nil {
			_ = sink.Close()
			t.Fatalf("wait for ct_server: %v", werr)
		}
	case <-time.After(120 * time.Second):
		timedOut = true
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		<-done
	}
	_ = sink.Close()
	tail, _ := os.ReadFile(errPath)
	said := strings.Contains(string(tail), "Invalid config:")
	switch {
	case timedOut:
		return false, "ct_server was still running two minutes after being given " + filepath.Base(cfgPath) + "; the configuration was accepted, not refused"
	case code != 1:
		return false, fmt.Sprintf("ct_server exited %d, not 1; stderr tail: %s", code, lastChars(tail, 400))
	case !said:
		return false, "ct_server exited 1 but never reported \"Invalid config:\"; stderr tail: " + lastChars(tail, 400)
	}
	return true, ""
}

func lastChars(b []byte, n int) string {
	if len(b) > n {
		b = b[len(b)-n:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

func main() {
	t := vhelp.New()

	// --- accept: one backend, one log bound to it. ---------------------------
	one, err := ctfe.MultiLogConfigFromFile(materialise(t, "multi-valid.textproto"))
	if err != nil {
		t.Fatalf("pinned multi-valid.textproto did not load: %v", err)
	}
	beMap, err := ctfe.ValidateLogMultiConfig(one)
	if err != nil {
		t.Check("accepts-single-backend", false, "ValidateLogMultiConfig: %v", err)
		t.Check("single-backend-map-spec", false, "no map returned")
	} else {
		t.Equal("accepts-single-backend", strings.Join(sortedNames(beMap), ","), "default")
		t.Equal("single-backend-map-spec", beMap["default"].GetBackendSpec(), "127.0.0.1:1")
	}

	// --- accept: two backends, one log each. --------------------------------
	two, err := ctfe.MultiLogConfigFromFile(materialise(t, "multi-two-backends.textproto"))
	if err != nil {
		t.Fatalf("pinned multi-two-backends.textproto did not load: %v", err)
	}
	beMap2, err := ctfe.ValidateLogMultiConfig(two)
	if err != nil {
		t.Check("accepts-two-backends", false, "ValidateLogMultiConfig: %v", err)
		t.Check("two-backend-map-specs", false, "no map returned")
	} else {
		t.Equal("accepts-two-backends", strings.Join(sortedNames(beMap2), ","), "backend-a,backend-b")
		t.Equal("two-backend-map-specs",
			beMap2["backend-a"].GetBackendSpec()+"|"+beMap2["backend-b"].GetBackendSpec(),
			"127.0.0.1:1|127.0.0.1:2")
	}

	// --- reject: the log names a backend nobody declared. --------------------
	undef, err := ctfe.MultiLogConfigFromFile(materialise(t, "multi-unknown-backend.textproto"))
	if err != nil {
		t.Fatalf("pinned multi-unknown-backend.textproto did not load: %v", err)
	}
	_, err = ctfe.ValidateLogMultiConfig(undef)
	t.Check("rejects-undefined-backend", err != nil, "undefined backend reference accepted")

	// --- reject: an unnamed backend. ----------------------------------------
	_, err = ctfe.ValidateLogMultiConfig(multi(
		[]*configpb.LogBackend{backend("", "127.0.0.1:1")},
		[]*configpb.LogConfig{logCfg(1, "bench", "")}))
	t.Check("rejects-empty-backend-name", err != nil, "backend with empty name accepted")

	// --- reject: a backend with no RPC endpoint. -----------------------------
	_, err = ctfe.ValidateLogMultiConfig(multi(
		[]*configpb.LogBackend{backend("default", "")},
		[]*configpb.LogConfig{logCfg(1, "bench", "default")}))
	t.Check("rejects-empty-backend-spec", err != nil, "backend with empty spec accepted")

	// --- accept: two logs sharing a prefix. ----------------------------------
	// v1.0.5's whole-file validator checks backend names, backend specs and
	// that every log names a declared backend. It does not look at prefixes:
	// "two logs with the same prefix" and "a log with no prefix" arrive at
	// v1.0.10 (CONFIG.md section 4.4), and the prefixes are not unique until
	// the handler map is built, where the second log loses the race for the
	// same URL path. So the corpus fixture named for the collision is, at this
	// release, accepted -- the name describes the defect, not the verdict at
	// v1.0.5. Grading it as a rejection here would grade v1.0.10's rule.
	dupPrefix, err := ctfe.MultiLogConfigFromFile(materialise(t, "multi-dup-prefix.textproto"))
	if err != nil {
		t.Fatalf("pinned multi-dup-prefix.textproto did not load: %v", err)
	}
	_, err = ctfe.ValidateLogMultiConfig(dupPrefix)
	t.Check("accepts-duplicate-prefix-at-this-release", err == nil,
		"ValidateLogMultiConfig rejected two logs sharing a prefix, a check that arrives at v1.0.10: %v", err)

	// --- reject: two backends sharing a name. --------------------------------
	_, err = ctfe.ValidateLogMultiConfig(multi(
		[]*configpb.LogBackend{backend("dup", "127.0.0.1:1"), backend("dup", "127.0.0.1:2")},
		[]*configpb.LogConfig{logCfg(1, "bench", "dup")}))
	t.Check("rejects-duplicate-backend-name", err != nil, "duplicate backend name accepted")

	// --- the binary refuses both shapes too. ---------------------------------
	// The undefined-backend file is the same pinned fixture the library case
	// above used. The duplicate-name file is derived from the valid two-backend
	// fixture by giving the second backend the first one's name, so the
	// duplicate is the only defect in it.
	ok, why := refused(t, "undefined-backend", materialise(t, "multi-unknown-backend.textproto"))
	t.Check("binary-refuses-a-log-naming-an-undefined-backend", ok, "%s", why)

	dupName := strings.ReplaceAll(
		string(vhelp.ReadFixture("configs", "multi-two-backends.textproto")), "backend-b", "backend-a")
	dupName = strings.ReplaceAll(dupName, "@FIXTURES@", vhelp.Fixture())
	dupName = strings.ReplaceAll(dupName, "@BACKEND_A@", "127.0.0.1:1")
	dupName = strings.ReplaceAll(dupName, "@BACKEND_B@", "127.0.0.1:2")
	dupPath := filepath.Join(os.Getenv("CT_SCRATCH"), "multi-dup-backend-name.textproto")
	if err := os.WriteFile(dupPath, []byte(dupName), 0600); err != nil {
		t.Fatalf("write %s: %v", dupPath, err)
	}
	ok, why = refused(t, "duplicate-backend-name", dupPath)
	t.Check("binary-refuses-a-duplicate-backend-name", ok, "%s", why)

	t.Done()
}

func sortedNames(m map[string]*configpb.LogBackend) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}
