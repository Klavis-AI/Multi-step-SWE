// Group gate-endtoend -- KIND gate.
//
// Proof of life plus the inherited end-to-end path, and nothing else. The
// candidate's own ct_server binary is started as a subprocess over real
// loopback sockets against a pinned single-backend LogConfigSet -- the config
// shape step 01 introduced, which v1.0.5 converts to a LogMultiConfig on load
// rather than superseding -- and it is asked one question that is answered
// entirely from the configured root pool:
//
//	the process answered on its HTTP endpoint
//	GET /<prefix>/ct/v1/get-roots returned 200
//	the DER set it returned is exactly the configured roots_pem_file
//
// Nothing v1.0.13 adds appears here. The thirteen cases that used to make up
// this group -- loglist, ctutil, x509util.ParseSCTsFromCertificate and the
// findlog binary -- are this step's headline new capability, none of it exists
// at v1.0.10, and the driver did not even compile there. In the gate that was
// an unpriced veto (gate rule 4) and it made this step's own no-op
// continuation report a FAILED GATE instead of a zero score. Those cases moved
// verbatim to the scored group ctutil-endtoend-verification at weight 3; no
// coverage was dropped.
//
// Determinism. No clock is read and no elapsed time is asserted: the poll loop
// below is a readiness wait, not a measurement, and the two verdicts it feeds
// are "the process answered" and "the status was 200". The root set is
// compared as a sorted set of hex DER, so neither the order the personality
// serves them in nor the order the PEM file is walked can change the answer.
// Both ports come from binding :0 on loopback, and the HTTP one is handed to
// the child the only way a child can be given a port -- bind, read, close,
// pass. The gRPC listener exists only because ct_server dials its backend
// before it serves; no service is registered on it because get-roots is
// answered from the in-memory pool and never reaches a backend. The child is
// SIGKILLed before any verdict is printed, because vhelp.T.Done exits the
// process and a deferred stop would never run.
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

// The prefix in logconfig.textproto.in, and the root pool it names.
const (
	logPrefix = "gate"
	rootsPEM  = "roots-bench.pem"
)

func main() {
	t := vhelp.New()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("Getwd: %v", err)
	}

	grpcLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for the backend: %v", err)
	}
	gs := grpc.NewServer()
	go gs.Serve(grpcLn)

	tmpl, err := os.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		gs.Stop()
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfgPath := filepath.Join(wd, "logs.textproto")
	cfg := strings.Replace(string(tmpl), "@FIXTURES@", vhelp.Fixture(), -1)
	if err := os.WriteFile(cfgPath, []byte(cfg), 0644); err != nil {
		gs.Stop()
		t.Fatalf("write config: %v", err)
	}

	httpAddr := lowBandLoopbackAddr(t)

	stdout := &bytes.Buffer{}
	stderr := &bytes.Buffer{}
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfgPath,
		"--log_rpc_server="+grpcLn.Addr().String(),
		"--http_endpoint="+httpAddr,
		"--metrics_endpoint=",
		"--get_sth_interval=0",
		"--log_dir="+wd,
	)
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		gs.Stop()
		t.Fatalf("start ct_server: %v", err)
	}
	stop := func() {
		if cmd.Process != nil {
			cmd.Process.Signal(syscall.SIGKILL)
			cmd.Wait()
		}
		gs.Stop()
	}

	url := "http://" + httpAddr + "/" + logPrefix + "/ct/v1/get-roots"
	status := 0
	up := false
	var body []byte
	for i := 0; i < 600; i++ {
		rsp, err := http.Get(url)
		if err == nil {
			status = rsp.StatusCode
			body, _ = io.ReadAll(rsp.Body)
			rsp.Body.Close()
			up = true
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	stop()

	t.Check("ct-server-starts-and-answers-on-its-http-endpoint", up,
		"the server never answered on %s; stderr=%s", url, tail(stderr.String()))
	t.Check("get-roots-answers-200-on-the-configured-prefix", status == http.StatusOK,
		"GET %s answered %d; stderr=%s", url, status, tail(stderr.String()))

	var rsp struct {
		Certificates [][]byte `json:"certificates"`
	}
	got := []string{}
	jsonErr := json.Unmarshal(body, &rsp)
	if jsonErr == nil {
		for _, d := range rsp.Certificates {
			got = append(got, hex.EncodeToString(d))
		}
	}
	want := []string{}
	for _, d := range pemDERs(vhelp.ReadFixture("minted", rootsPEM)) {
		want = append(want, hex.EncodeToString(d))
	}
	sort.Strings(got)
	sort.Strings(want)
	t.Check("get-roots-serves-exactly-the-configured-root-pool",
		len(want) > 0 && jsonErr == nil && strings.Join(got, ",") == strings.Join(want, ","),
		"json=%v, got %d root(s), want %d; body=%s", jsonErr, len(got), len(want), tail(string(body)))

	t.Done()
}

// pemDERs returns the DER body of every CERTIFICATE block in b. It uses the
// standard library rather than the product's x509 fork: this group observes
// the candidate only across the process boundary.
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
