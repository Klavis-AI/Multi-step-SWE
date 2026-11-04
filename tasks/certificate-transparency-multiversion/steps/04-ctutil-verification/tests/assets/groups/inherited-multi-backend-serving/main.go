// Group inherited-multi-backend-serving -- KIND gate.
//
// Step 04's other gate groups all land on step 01's milestone -- the RFC 6962
// stack, merkletree and the log client. This one covers step 02's: one
// ct_server fronting several Trillian backends at once, with each configured
// log bound by name to the backend its own config names. Nothing in v1.0.13
// touches that, and it is the milestone a verification-library step could most
// easily break without noticing, because every ctutil case would still pass.
//
// One deterministic end-to-end path, no more. The candidate's own ct_server is
// started against a LogMultiConfig that declares two backends on two loopback
// ports and binds log "bencha" to the first and log "benchb" to the second.
// The two logs are given DIFFERENT root pools -- one root, and that root plus
// an intermediate -- so a personality that registered only the first config,
// or reused one config for both prefixes, answers with the wrong DER set and
// is caught. get-roots is answered entirely from the in-memory pool the config
// named, so no backend traffic is needed and neither stub has to implement
// anything.
//
// Deliberately NOT here. Which backend a request is actually routed to cannot
// be observed from outside without a Trillian stub, and the trillian API
// breaks across this step's two anchors -- SignedLogRoot.LogId exists at
// v1.0.7-0.20180223125122-7c253b1ef81d and is gone at v1.1.0 -- so no single
// driver compiles at both. A gate group that does not compile at the previous
// milestone is exactly the fault this re-tier exists to remove, so the routing
// assertion stays out and this group asserts only what it can prove there.
//
// Determinism. No clock is read and no elapsed time is asserted: the poll loop
// is a readiness wait, not a measurement. Every port comes from binding :0 on
// loopback, and the HTTP one is handed to the child the only way a child can
// be given a port -- bind, read, close, pass. Root sets are compared as sorted
// hex DER, so no serving or file-walk order can change an answer. The child is
// SIGKILLed before any verdict is printed, because vhelp.T.Done exits the
// process and a deferred stop would never run. ct_server does not use
// grpc.WithBlock when its config names more than one backend, so nothing here
// asserts that either stub was dialled.
package main

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"time"

	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// The two prefixes in logconfig.textproto.in and the root pool each names.
const (
	prefixA = "bencha"
	rootsA  = "roots-bench.pem"
	prefixB = "benchb"
	rootsB  = "roots-bench-plus-int.pem"
)

type reply struct {
	up     bool
	status int
	body   []byte
}

func main() {
	t := vhelp.New()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("Getwd: %v", err)
	}

	backendA, srvA, err := backend()
	if err != nil {
		t.Fatalf("listen for backend-a: %v", err)
	}
	backendB, srvB, err := backend()
	if err != nil {
		srvA.Stop()
		t.Fatalf("listen for backend-b: %v", err)
	}
	stopBackends := func() { srvA.Stop(); srvB.Stop() }

	tmpl, err := os.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		stopBackends()
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfg := string(tmpl)
	cfg = strings.Replace(cfg, "@FIXTURES@", vhelp.Fixture(), -1)
	cfg = strings.Replace(cfg, "@BACKEND_A@", backendA, -1)
	cfg = strings.Replace(cfg, "@BACKEND_B@", backendB, -1)
	cfgPath := filepath.Join(wd, "multi.textproto")
	if err := os.WriteFile(cfgPath, []byte(cfg), 0644); err != nil {
		stopBackends()
		t.Fatalf("write config: %v", err)
	}

	httpAddr := lowBandLoopbackAddr(t)

	stderr := &bytes.Buffer{}
	// --log_rpc_server is emptied on purpose: ct_server reads --log_config as a
	// LogMultiConfig only when no backend is given on the command line.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfgPath,
		"--log_rpc_server=",
		"--http_endpoint="+httpAddr,
		"--metrics_endpoint=",
		"--get_sth_interval=0",
		"--log_dir="+wd,
	)
	cmd.Stdout = &bytes.Buffer{}
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		stopBackends()
		t.Fatalf("start ct_server: %v", err)
	}
	stop := func() {
		if cmd.Process != nil {
			cmd.Process.Signal(syscall.SIGKILL)
			cmd.Wait()
		}
		stopBackends()
	}

	base := "http://" + httpAddr
	a := poll(base + "/" + prefixA + "/ct/v1/get-roots")
	b := reply{}
	if a.up {
		b = get(base + "/" + prefixB + "/ct/v1/get-roots")
	}
	stop()

	t.Check("ct-server-starts-from-a-two-backend-log-multi-config", a.up,
		"the server never answered on %s; stderr=%s", base, tail(stderr.String()))

	checkPool(t, "the-log-bound-to-the-first-backend-serves-its-own-root-pool",
		prefixA, rootsA, a, stderr)
	checkPool(t, "the-log-bound-to-the-second-backend-serves-its-own-root-pool",
		prefixB, rootsB, b, stderr)

	t.Done()
}

// checkPool records one case: the prefix answered 200 and the DER set it
// served is exactly the root pool its own config named.
func checkPool(t *vhelp.T, id, prefix, roots string, r reply, stderr *bytes.Buffer) {
	if !r.up || r.status != http.StatusOK {
		t.Check(id, false, "GET /%s/ct/v1/get-roots answered %d (reached=%v); stderr=%s",
			prefix, r.status, r.up, tail(stderr.String()))
		return
	}
	var rsp struct {
		Certificates [][]byte `json:"certificates"`
	}
	if err := json.Unmarshal(r.body, &rsp); err != nil {
		t.Check(id, false, "/%s get-roots body did not decode: %v", prefix, err)
		return
	}
	got := []string{}
	for _, d := range rsp.Certificates {
		got = append(got, hex.EncodeToString(d))
	}
	want := []string{}
	for _, d := range pemDERs(vhelp.ReadFixture("minted", roots)) {
		want = append(want, hex.EncodeToString(d))
	}
	sort.Strings(got)
	sort.Strings(want)
	t.Check(id, len(want) > 0 && strings.Join(got, ",") == strings.Join(want, ","),
		"/%s served %d root(s), its config names %s with %d", prefix, len(got), roots, len(want))
}

// backend brings up a gRPC server on a loopback port with no service
// registered. ct_server dials every backend its config names; get-roots never
// reaches one, so nothing has to answer.
func backend() (string, *grpc.Server, error) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return "", nil, err
	}
	s := grpc.NewServer()
	go s.Serve(ln)
	return ln.Addr().String(), s, nil
}

func poll(url string) reply {
	for i := 0; i < 600; i++ {
		r := get(url)
		if r.up {
			return r
		}
		time.Sleep(50 * time.Millisecond)
	}
	return reply{}
}

func get(url string) reply {
	rsp, err := http.Get(url)
	if err != nil {
		return reply{}
	}
	defer rsp.Body.Close()
	body, _ := io.ReadAll(rsp.Body)
	return reply{up: true, status: rsp.StatusCode, body: body}
}

// pemDERs returns the DER body of every CERTIFICATE block in b, using the
// standard library: this group observes the candidate across the process
// boundary only.
func pemDERs(b []byte) [][]byte {
	var out [][]byte
	rest := b
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			return out
		}
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
	}
}

func tail(s string) string {
	if len(s) > 300 {
		return "..." + s[len(s)-300:]
	}
	return s
}

// lowBandLoopbackAddr returns a loopback host:port taken from BELOW the
// kernel's ephemeral range (32768+ on the harness host), probed free by an
// actual bind. A `:0` probe hands back a port the kernel may immediately
// reassign to any unrelated process on the machine in the window before it is
// used; a low-band port is never handed out by the ephemeral allocator, so
// only an explicit binder of that exact port can take it. The starting slot is
// dispersed by one throwaway `:0` bind so that sibling probes in the same run
// do not converge on the same slot.
func lowBandLoopbackAddr(t *vhelp.T) string {
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0 to disperse the probe: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close the dispersal listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		l, err := net.ListenTCP("tcp", &net.TCPAddr{
			IP:   net.IPv4(127, 0, 0, 1),
			Port: 20000 + (base+i)%12000,
		})
		if err != nil {
			continue
		}
		a := l.Addr().String()
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return a
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return ""
}
