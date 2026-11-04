// Group scanlog-end-index.
//
// The Fetcher extraction (commit 83a60276) gives scanlog a stopping point:
// v1.0.17 adds --end_index, "Log index to end scanning at (non-inclusive,
// 0 = end of log)", and threads it through ScannerOptions.FetcherOptions.
// v1.0.13's scanlog could only say --start_index and then ran to the head of
// the log.
//
// Graded end to end on the candidate's own scanlog binary, against a loopback
// CT log served by this driver (see fakelog.go). The observable is what the
// binary writes: --dump_dir names each file after the entry index, so the
// exact set of filenames is the scanned range, and the log's own request
// record shows the bound was respected on the wire and not just in the
// output filter.
//
// SCOPE-NET: 127.0.0.1, port from binding :0. SCOPE-TIME: every entry
// timestamp is a constant.
//
// The driver compiles at both anchors -- it only uses ct/tls types -- and at
// v1.0.13 the binary rejects --end_index outright, dumps nothing, and exits
// non-zero, so the group scores zero there.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const treeSize = 12

// runScanlog runs the candidate's scanlog with a watchdog and returns its exit
// code (-1 if it had to be killed) and its combined output.
func runScanlog(t *vhelp.T, args ...string) (int, string) {
	cmd := vhelp.Command(vhelp.CandidateBin("scanlog"), args...)
	out, err := func() ([]byte, error) {
		done := make(chan struct{})
		var b strings.Builder
		cmd.Stdout = &b
		cmd.Stderr = &b
		if err := cmd.Start(); err != nil {
			return nil, err
		}
		go func() {
			select {
			case <-done:
			case <-time.After(90 * time.Second):
				cmd.Process.Kill()
			}
		}()
		werr := cmd.Wait()
		close(done)
		return []byte(b.String()), werr
	}()
	if err != nil {
		if cmd.ProcessState != nil {
			return cmd.ProcessState.ExitCode(), string(out)
		}
		t.Fatalf("run scanlog: %v", err)
	}
	return 0, string(out)
}

// flagDeclared reports whether usage text declares exactly this flag name (as
// opposed to a longer name that merely starts with it).
func flagDeclared(usage, flag string) bool {
	for _, line := range strings.Split(usage, "\n") {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "-"+flag) {
			continue
		}
		rest := strings.TrimPrefix(line, "-"+flag)
		if rest == "" || strings.HasPrefix(rest, " ") || strings.HasPrefix(rest, "=") {
			return true
		}
	}
	return false
}

// dumped lists the basenames written into dir, sorted.
func dumped(t *vhelp.T, dir string) []string {
	ents, err := os.ReadDir(dir)
	if err != nil {
		t.Fatalf("read dump dir: %v", err)
	}
	var names []string
	for _, e := range ents {
		names = append(names, e.Name())
	}
	sort.Strings(names)
	return names
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	emptyChain, err := tls.Marshal(ct.CertificateChain{})
	if err != nil {
		t.Fatalf("marshal empty chain: %v", err)
	}
	entries := make([]fakeEntry, treeSize)
	for i := range entries {
		leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, fakeSTHTimestamp+uint64(i))
		b, err := tls.Marshal(*leaf)
		if err != nil {
			t.Fatalf("marshal leaf %d: %v", i, err)
		}
		entries[i] = fakeEntry{LeafInput: b, ExtraData: emptyChain}
	}

	log, err := startFakeLog(entries, 5)
	if err != nil {
		t.Fatalf("start loopback log: %v", err)
	}
	defer log.Close()

	scratch := os.Getenv("CT_SCRATCH")
	bounded := filepath.Join(scratch, "bounded")
	whole := filepath.Join(scratch, "whole")
	for _, d := range []string{bounded, whole} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			t.Fatalf("mkdir %s: %v", d, err)
		}
	}

	// --- the bounded scan ---------------------------------------------------
	before := len(log.Ranges())
	rc, _ := runScanlog(t,
		"--log_uri="+log.URL,
		"--start_index=2", "--end_index=7",
		"--batch_size=3", "--parallel_fetch=1", "--num_workers=1",
		"--quiet", "--dump_dir="+bounded)
	asked := log.Ranges()[before:]

	// 1. it exits cleanly.
	t.Equal("bounded-scan-exit-status", rc, 0)

	// 2. and dumps exactly entries 2..6 -- five of the twelve.
	var want []string
	for i := 2; i <= 6; i++ {
		want = append(want, fmt.Sprintf("cert-%014d-leaf.der", i))
	}
	t.Equal("end-index-bounds-the-dumped-range",
		strings.Join(dumped(t, bounded), " "), strings.Join(want, " "))

	// 3. the bound is honoured on the wire: [2,4] then [5,6], never past 6.
	t.Equal("end-index-bounds-the-get-entries-requests",
		fmt.Sprintf("%v", asked), "[[2 4] [5 6]]")

	// 4. what it wrote is the entry's own certificate, byte for byte.
	got, err := os.ReadFile(filepath.Join(bounded, "cert-00000000000004-leaf.der"))
	if err != nil {
		t.Check("dumped-leaf-is-the-logged-certificate", false, "read dumped leaf: %v", err)
	} else {
		t.EqualBytes("dumped-leaf-is-the-logged-certificate", got, leafDER)
	}

	// 5. control: with the flag left at its zero default the same invocation
	// scans the whole tree, so case 2 is measuring --end_index and not some
	// unrelated refusal to fetch.
	rc, _ = runScanlog(t,
		"--log_uri="+log.URL,
		"--start_index=0", "--end_index=0",
		"--batch_size=3", "--parallel_fetch=1", "--num_workers=1",
		"--quiet", "--dump_dir="+whole)
	want = want[:0]
	for i := 0; i < treeSize; i++ {
		want = append(want, fmt.Sprintf("cert-%014d-leaf.der", i))
	}
	sort.Strings(want)
	if rc != 0 {
		t.Check("zero-end-index-scans-the-whole-log", false, "scanlog exit %d", rc)
	} else {
		t.Equal("zero-end-index-scans-the-whole-log",
			strings.Join(dumped(t, whole), " "), strings.Join(want, " "))
	}

	// --- 6. and the binary declares the flag in its own usage text ---------
	// The shape that moved out of the gate group build-clean, which is proof
	// of life and may therefore only assert flags a step-04 tree already has.
	// Read from the command's public interface, never from a source file.
	help := vhelp.Command(vhelp.CandidateBin("scanlog"), "--help")
	var helpOut strings.Builder
	help.Stdout = &helpOut
	help.Stderr = &helpOut
	// Only the usage text is read here, so the exit status is deliberately
	// discarded. It is not non-zero: CLI.md's preamble pins `-h`/`--help`
	// to 0, and the gate group build-clean case 4 asserts that across all
	// twelve flag-driven commands. This group simply has no business in it.
	_ = help.Run()
	t.Check("scanlog-usage-declares-end-index", flagDeclared(helpOut.String(), "end_index"),
		"scanlog usage does not declare -end_index")

	t.Done()
}
