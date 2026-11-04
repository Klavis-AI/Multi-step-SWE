// Group gate-endtoend.
//
// Step 1 inherits no milestone, so under rule 5 of "The gate" this group is
// proof of life and nothing else: the candidate's own ct_server binary starts
// against a pinned configuration and answers on the prefix that configuration
// names. It earns no credit; it can only veto.
//
// Everything this milestone's front end actually *does* -- the root pool it
// publishes, the SCTs it issues and their signatures, admission control, the
// read endpoints, the backend error map, the error envelope, the shutdown
// contract -- is capability this step introduces. It is priced in the scored
// group `ctfe-endtoend-over-socket` and none of it is asserted here. A gate
// that asserted it would be an unpriced veto: a candidate that built the whole
// stack but got one corner of the personality wrong would score zero for the
// step rather than lose that corner's weight.
//
// Determinism. No clock is read, no backend value is examined, no body is
// compared: the two verdicts are "the process answered" and "the status was
// 200". The gRPC listener exists only because v1.0's ct_server dials its
// backend with grpc.WithBlock() before it serves (CLI.md s2, ct_server main);
// no service is registered on it because get-roots never reaches the backend.
// The two ports are obtained differently, on purpose. The gRPC port comes from
// binding :0, which is safe because this process keeps that listener and serves
// on it. The HTTP port cannot work that way -- it has to be handed to a child --
// so it comes from lowBandLoopbackAddr, which bind-tests a port below the kernel
// ephemeral range before returning it; binding :0 and closing would leave a
// window in which the kernel could hand the same port to another test. The child
// is SIGKILLed on every path out of main, because vhelp.T.Done exits the
// process and a deferred stop would never run.
package main

import (
	"bytes"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"google.golang.org/grpc"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	scratch, err := ioutil.TempDir("", "gate01")
	if err != nil {
		t.Fatalf("TempDir: %v", err)
	}

	grpcLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	go gs.Serve(grpcLn)

	tmpl, err := ioutil.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfgPath := filepath.Join(scratch, "logs.textproto")
	cfg := strings.Replace(string(tmpl), "@FIXTURES@", vhelp.Fixture(), -1)
	if err := ioutil.WriteFile(cfgPath, []byte(cfg), 0644); err != nil {
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
		"--log_dir="+scratch,
	)
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	if err := cmd.Start(); err != nil {
		t.Fatalf("start ct_server: %v", err)
	}
	stop := func() {
		if cmd.Process != nil {
			cmd.Process.Signal(syscall.SIGKILL)
			cmd.Wait()
		}
		gs.Stop()
		os.RemoveAll(scratch)
	}

	base := "http://" + httpAddr
	status := 0
	up := false
	for i := 0; i < 600; i++ {
		rsp, err := http.Get(base + "/gate/ct/v1/get-roots")
		if err == nil {
			status = rsp.StatusCode
			rsp.Body.Close()
			up = true
			break
		}
		time.Sleep(50 * time.Millisecond)
	}

	t.Check("ct-server-starts-and-answers-on-its-http-endpoint", up,
		"the server never answered on %s; stderr=%s", base, tail(stderr.String()))
	t.Check("the-configured-log-prefix-is-served", status == 200,
		"GET /gate/ct/v1/get-roots answered %d; stderr=%s", status, tail(stderr.String()))

	stop()
	t.Done()
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
