// migrillian-etcd-flags-removed
//
// v1.3.3 drops etcd-based mastership from Migrillian. The three flags that
// configured it -- --etcd_servers, --lock_file_path and --election_delay --
// are removed from the flag set, so the binary now refuses them at parse time
// with the standard flag-package diagnostic and exit status 2, where v1.2.0
// accepted all three and went on to the --backend check.
//
// With etcd gone there is no election mechanism left, so the release also
// makes --force_master mandatory: a run without it stops at the mastership
// step with exit 1 and a message naming etcd (CLI.md section 4.7). Only that
// name is matched; the sentence it sits in is the product's own. That check sits behind the --backend and --config
// checks, so the last two cases give the binary a backend that answers and a
// config that loads, and then vary nothing but --force_master.
//
// This is a command-line capability, so it is graded by running the candidate
// binary as a subprocess and comparing its exit status and its diagnostic
// output. Nothing is imported from the product.
//
// SCOPE-BACKEND: the listener the last two cases hand to --backend is a bare
// gRPC server with no services registered. google/trillian is not a
// deliverable and nothing is asserted about it; the socket exists only
// because grpc.Dial is given WithBlock and migrillian will not walk past a
// backend it cannot reach. It exits before issuing a single RPC.
// SCOPE-NET: loopback only, every port obtained by binding :0.
package main

import (
	"bytes"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// run executes migrillian with the given arguments and returns its exit status
// and its combined output. The child never shares this process's stdout, and
// vhelp.Command has already stripped CT_VERIFY_NONCE from its environment.
func run(t *vhelp.T, args ...string) (int, string) {
	cmd := vhelp.Command(vhelp.CandidateBin("migrillian"), args...)
	var buf bytes.Buffer
	cmd.Stdout = &buf
	cmd.Stderr = &buf
	if err := cmd.Start(); err != nil {
		t.Fatalf("starting migrillian %v: %v", args, err)
	}
	// None of the invocations below is meant to keep running: each one stops
	// at a start-up check, or finds no log to migrate and returns. The timer
	// is containment for the case where a candidate blocks anyway -- the
	// child is killed and reaped here rather than left behind.
	kill := time.AfterFunc(60*time.Second, func() { _ = cmd.Process.Kill() })
	err := cmd.Wait()
	kill.Stop()
	switch e := err.(type) {
	case nil:
		return 0, buf.String()
	case *exec.ExitError:
		return e.ExitCode(), buf.String()
	default:
		t.Fatalf("running migrillian %v: %v", args, err)
		return -1, ""
	}
}

const (
	// Printed by main() once flag parsing has succeeded. Seeing it proves the
	// flags on the command line were all accepted.
	parsed = "--backend flag must be specified"
	// The flag package writes the diagnostic for a name that is not
	// registered, and section 3.3 forbids grading a standard-library message
	// on its own account, so only the identifier inside it is matched: the
	// name of the flag that is gone. Exit 2 -- fixed by CLI.md's preamble --
	// carries the rest of the verdict.
)

func main() {
	t := vhelp.New()

	// 1. The binary exists, runs, parses an empty command line and reaches the
	//    --backend check. Without this every "flag rejected" case below could
	//    be satisfied by a binary that does not work at all.
	rc, out := run(t)
	t.Check("control-bare-invocation-reaches-the-backend-check",
		rc == 1 && strings.Contains(out, parsed),
		"migrillian with no arguments exited %d: %s", rc, oneLine(out))

	// 2. A flag name that was never in any release is rejected with exit 2 and
	//    names itself. This pins the shape the removal cases look for.
	rc, out = run(t, "--harbor_no_such_flag=1")
	t.Check("control-unregistered-flag-is-rejected-with-status-2",
		rc == 2 && strings.Contains(out, "harbor_no_such_flag"),
		"unknown flag exited %d: %s", rc, oneLine(out))

	// 3-4. The two flags this release keeps are still accepted: parsing
	//      succeeds and main() reaches the --backend check.
	for _, kept := range []struct{ id, arg string }{
		{"control-force-master-flag-still-accepted", "--force_master"},
		{"control-config-flag-still-accepted", "--config=/nonexistent/migrillian.cfg"},
	} {
		rc, out = run(t, kept.arg)
		t.Check(kept.id, rc == 1 && strings.Contains(out, parsed),
			"migrillian %s exited %d: %s", kept.arg, rc, oneLine(out))
	}

	// 5-7. The three etcd flags are gone from the flag set.
	for _, removed := range []struct{ id, name, arg string }{
		{"etcd-servers-flag-removed", "etcd_servers", "--etcd_servers=localhost:2379"},
		{"lock-file-path-flag-removed", "lock_file_path", "--lock_file_path=/migrillian/master"},
		{"election-delay-flag-removed", "election_delay", "--election_delay=5s"},
	} {
		rc, out = run(t, removed.arg)
		t.Check(removed.id,
			rc == 2 && strings.Contains(out, removed.name) &&
				!strings.Contains(out, parsed),
			"migrillian %s exited %d: %s", removed.arg, rc, oneLine(out))
	}

	// 8-9. The mastership check. Reaching it means getting past the two checks
	//      in front of it, so both runs are given a backend that answers and a
	//      config that loads; the only difference between them is
	//      --force_master. An empty migration set is deliberate: the run has
	//      nothing to migrate, so the control run reaches the end of its work
	//      and returns instead of settling into a migration loop.
	addr, stop := backendStub(t)
	defer stop()
	cfg := emptyConfig(t)
	base := []string{"--backend=" + addr, "--config=" + cfg, "--metrics_endpoint=localhost:0"}

	rc, out = run(t, append([]string{"--force_master"}, base...)...)
	t.Check("control-with-force-master-the-run-is-not-refused",
		rc == 0 && !refusesEtcd(out),
		"migrillian --force_master against a live backend exited %d: %s", rc, oneLine(out))

	rc, out = run(t, base...)
	t.Check("without-force-master-migrillian-refuses-to-run",
		rc == 1 && refusesEtcd(out),
		"migrillian without --force_master exited %d: %s", rc, oneLine(out))

	t.Done()
}

// refusesEtcd reports whether the output carries this release's mastership
// refusal (CLI.md section 4.7). The only thing matched is the one identifier
// the documents fix -- the name of the dependency that left -- because
// TEST_INTERFACE.md section 3.3 allows a diagnostic to be graded on what it
// identifies and never on the sentence around it. The refusal is the sole
// channel through which "this run stopped because there is no election
// mechanism left" is observable at all: exit 1 alone does not distinguish it
// from the config and backend failures that share that status.
func refusesEtcd(out string) bool {
	return strings.Contains(strings.ToLower(out), "etcd")
}

// backendStub starts a gRPC server on a loopback port and returns its address
// and a function that shuts it down. Nothing is registered on it: the only
// thing asked of it is that a dial with WithBlock completes.
func backendStub(t *vhelp.T) (string, func()) {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listening for the backend stub: %v", err)
	}
	gs := grpc.NewServer()
	go func() { _ = gs.Serve(lis) }()
	return lis.Addr().String(), gs.Stop
}

// emptyConfig writes a MigrillianConfig with a present but empty migration
// set and returns its path. It has to be present -- the loaded config is
// walked, not just validated -- and it has to be empty, or the run would need
// a real CT log to copy.
func emptyConfig(t *vhelp.T) string {
	path := filepath.Join(os.Getenv("CT_SCRATCH"), "migrillian.cfg")
	if err := os.WriteFile(path, []byte("migration_configs: {}\n"), 0o644); err != nil {
		t.Fatalf("writing %s: %v", path, err)
	}
	return path
}

func oneLine(s string) string {
	s = strings.ReplaceAll(strings.TrimSpace(s), "\n", " | ")
	if len(s) > 300 {
		s = s[:300]
	}
	return fmt.Sprint(s)
}
