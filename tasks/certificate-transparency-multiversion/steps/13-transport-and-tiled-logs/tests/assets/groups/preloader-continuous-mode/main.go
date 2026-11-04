// preloader-continuous-mode
//
// v1.3.3 gives the preloader a bounded and a continuous fetch mode. Three new
// flags appear -- --end_index, --continuous and --target_bearer_token -- and a
// guard rejects the one combination that cannot mean anything: --continuous
// together with a non-zero --end_index, fatally and before any configuration
// is read (CLI.md 4.2).
//
// Graded at the command-line boundary, on control flow, and never on wording.
// CLI.md 4.2 fixes the condition, the exit status and the ordering, and says
// in terms that the refusal's wording is the product's own; TEST_INTERFACE.md
// section 3.3 forbids grading a diagnostic the documents do not fix. So the
// ordering is observed directly instead of being inferred from a message: the
// path handed to --target_temporal_log_cfg is a FIFO, and a FIFO can be opened
// for writing without blocking exactly when some reader already holds it open.
// Probing that open from here is a yes/no answer to the only question the
// ordering rule asks -- "did this run reach the temporal-config read?" -- and
// it is answered by the kernel, not by the candidate.
//
// Closing that write end immediately afterwards hands the reader an empty
// file, which every release of TemporalLogConfigFromFile rejects, so the run
// ends by itself with the exit 1 CLI.md documents for a fatal preloader error.
// Nothing here touches the network: client.New does not dial, and the scanner
// is not started until after the temporal config has been read.
package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"syscall"
	"time"

	"ctverify/vhelp"
)

// Bounds on a single probe. A run that reaches the FIFO blocks there until
// this driver unblocks it, so the wait is over in milliseconds in practice;
// the deadline only exists so that a candidate that hangs somewhere else
// cannot hang the group.
const (
	probeDeadline = 60 * time.Second
	pollInterval  = 20 * time.Millisecond
)

type outcome struct {
	opened bool   // the run opened the temporal-config path
	exited bool   // it finished on its own, rather than being killed
	rc     int    // its exit status, when it finished
	out    string // everything it wrote, for the failure message only
}

func (o outcome) String() string {
	s := strings.ReplaceAll(strings.TrimSpace(o.out), "\n", " | ")
	if len(s) > 300 {
		s = s[:300]
	}
	return fmt.Sprintf("opened=%v exited=%v rc=%d out=%q", o.opened, o.exited, o.rc, s)
}

// probe runs the preloader once with a fresh FIFO as its temporal-log-config
// path and reports whether the run got as far as opening that path.
func probe(t *vhelp.T, n int, args ...string) outcome {
	fifo := scratchPath(fmt.Sprintf("temporal-%02d.fifo", n))
	_ = os.Remove(fifo)
	if err := syscall.Mkfifo(fifo, 0o600); err != nil {
		t.Fatalf("mkfifo %s: %v", fifo, err)
	}
	defer os.Remove(fifo)

	cmd := vhelp.Command(vhelp.CandidateBin("preloader"),
		append(append([]string{}, args...), "--target_temporal_log_cfg="+fifo)...)
	var buf bytes.Buffer
	cmd.Stdout = &buf
	cmd.Stderr = &buf
	if err := cmd.Start(); err != nil {
		t.Fatalf("starting preloader %v: %v", args, err)
	}

	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()

	var o outcome
	deadline := time.After(probeDeadline)
	tick := time.NewTicker(pollInterval)
	defer tick.Stop()
wait:
	for {
		if !o.opened {
			// ENXIO here means "no reader"; success means the child has the
			// FIFO open, i.e. it reached the temporal-config read. Closing
			// at once gives that reader EOF so the run can finish.
			if fd, err := syscall.Open(fifo, syscall.O_WRONLY|syscall.O_NONBLOCK, 0); err == nil {
				_ = syscall.Close(fd)
				o.opened = true
			}
		}
		select {
		case err := <-done:
			o.exited = true
			if e, ok := err.(*exec.ExitError); ok {
				o.rc = e.ExitCode()
			} else if err != nil {
				t.Fatalf("waiting for preloader %v: %v", args, err)
			}
			break wait
		case <-deadline:
			break wait
		case <-tick.C:
		}
	}
	if !o.exited {
		_ = cmd.Process.Kill()
		<-done
		o.rc = -1
	}
	o.out = buf.String()
	return o
}

func scratchPath(name string) string {
	d := os.Getenv("CT_SCRATCH")
	if d == "" {
		d = "."
	}
	return strings.TrimSuffix(d, "/") + "/" + name
}

func main() {
	t := vhelp.New()

	// 1. The shape of a rejected flag name, pinned by a name no release ever
	//    had. CLI.md's preamble fixes exit 2 for an unrecognised flag; the
	//    flag package writes the sentence and nothing here reads it.
	cmd := vhelp.Command(vhelp.CandidateBin("preloader"), "--harbor_no_such_flag=1")
	var buf bytes.Buffer
	cmd.Stdout, cmd.Stderr = &buf, &buf
	rc := 0
	if err := cmd.Run(); err != nil {
		e, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("running preloader with an unknown flag: %v", err)
		}
		rc = e.ExitCode()
	}
	unknownOut := buf.String()
	t.Check("control-unregistered-flag-is-rejected-with-status-2",
		rc == 2,
		"preloader --harbor_no_such_flag=1 exited %d, want 2: %s", rc, strings.ReplaceAll(unknownOut, "\n", " | "))

	// 2. The baseline. With no new flag at all the preloader parses its
	//    command line and goes on to read the temporal log config, so the
	//    FIFO acquires a reader and the run then ends on the empty file.
	o := probe(t, 2)
	t.Check("control-run-reaches-the-temporal-config-read",
		o.opened && o.exited && o.rc == 1,
		"baseline run: %v; want the config path opened and exit 1", o)

	// 3-5. Each new flag is registered: the binary accepts it rather than
	//      exiting 2 at flag parsing, and the run still gets to the same
	//      point the baseline reached.
	for i, f := range []struct{ id, arg string }{
		{"continuous-flag-is-registered", "--continuous"},
		{"end-index-flag-is-registered", "--end_index=0"},
		{"target-bearer-token-flag-is-registered", "--target_bearer_token=harbor-token"},
	} {
		o = probe(t, 3+i, f.arg)
		t.Check(f.id,
			o.opened && o.exited && o.rc == 1,
			"preloader %s: %v; want the flag accepted, the config path opened and exit 1", f.arg, o)
	}

	// 6. --continuous with a non-zero --end_index is refused, by the guard,
	//    before the temporal config is ever read (CLI.md 4.2). Both halves
	//    are asserted: the run is fatal with exit 1, and it never opened the
	//    config path. What it prints on the way out is its own business.
	o = probe(t, 6, "--continuous", "--end_index=5")
	t.Check("continuous-with-non-zero-end-index-is-refused",
		o.exited && o.rc == 1 && !o.opened,
		"preloader --continuous --end_index=5: %v; want exit 1 with the config path never opened", o)

	// 7. The same command line with --end_index=0 passes the guard, and the
	//    run reaches the config read that case 6 proved it stops short of.
	o = probe(t, 7, "--continuous", "--end_index=0")
	t.Check("continuous-with-zero-end-index-passes-the-guard",
		o.opened && o.exited && o.rc == 1,
		"preloader --continuous --end_index=0: %v; want the config path opened and exit 1", o)

	// 8. The guard is the conjunction CLI.md 4.2 states, not either half of
	//    it: a non-zero --end_index on its own is an ordinary bounded run and
	//    must not be refused.
	o = probe(t, 8, "--end_index=7")
	t.Check("non-zero-end-index-without-continuous-is-not-refused",
		o.opened && o.exited && o.rc == 1,
		"preloader --end_index=7: %v; want the config path opened and exit 1", o)

	t.Done()
}
