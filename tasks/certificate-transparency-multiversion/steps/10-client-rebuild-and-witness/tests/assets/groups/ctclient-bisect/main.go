// Group ctclient-bisect.
//
// v1.1.3 re-implements the timestamp search as the Cobra subcommand `bisect`
// (alias `find-timestamp`) whose --timestamp/--chain/--text flags follow the
// command word. It fetches an STH, binary-searches the log by asking for one
// entry at a time, and then re-reads and renders the entry it found. This
// group serves a pinned eight-entry log over a loopback socket and asserts the
// exact stdout, together with the shape of the search: an STH first, then one
// get-entries request per probe naming a single index inside the tree, few
// enough of them to be a binary search rather than a scan, and -- when an entry
// is reported -- a probe of the index reported. CLI.md section 3.2 fixes that
// much and no more, so neither the order of the probes nor whether the tool
// re-reads the entry it found is compared.
//
// At v1.1.2 `bisect` is a trailing positional and -timestamp must precede it,
// so `bisect --timestamp=N` is not a valid invocation and every case fails.
package main

import (
	"encoding/base64"
	"encoding/pem"
	"fmt"
	"net/http"
	"net/url"
	"sort"
	"strconv"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	treeSize = 8
	baseTS   = uint64(1500000000000) // 2017-07-14 02:40:00 +0000 UTC
	stepTS   = uint64(3600000)       // one hour per entry
)

// entryTS is the pinned timestamp of entry i.
func entryTS(i int) uint64 { return baseTS + uint64(i)*stepTS }

// renderTime reproduces the tool's `%v` of a CT timestamp. The subprocess runs
// under TZ=UTC, so pinning UTC here makes the expectation independent of the
// machine the verifier runs on.
func renderTime(ms uint64) string {
	return time.UnixMilli(int64(ms)).UTC().String()
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }

func main() {
	t := vhelp.New()
	requireClient(t)

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")

	// leafInput[i] is the pinned MerkleTreeLeaf of entry i.
	leafInput := make([]string, treeSize)
	for i := 0; i < treeSize; i++ {
		l := ct.MerkleTreeLeaf{
			Version:  ct.V1,
			LeafType: ct.TimestampedEntryLeafType,
			TimestampedEntry: &ct.TimestampedEntry{
				Timestamp: entryTS(i),
				EntryType: ct.X509LogEntryType,
				X509Entry: &ct.ASN1Cert{Data: leafDER},
			},
		}
		b, err := tls.Marshal(l)
		if err != nil {
			t.Fatalf("tls.Marshal(leaf %d): %v", i, err)
		}
		leafInput[i] = b64(b)
	}
	emptyChain, err := tls.Marshal(ct.CertificateChain{})
	if err != nil {
		t.Fatalf("tls.Marshal(empty chain): %v", err)
	}
	extra := b64(emptyChain)

	sthJSON := fmt.Sprintf(
		`{"tree_size":%d,"timestamp":%d,"sha256_root_hash":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","tree_head_signature":"BAMAAjBF"}`,
		treeSize, baseTS)

	leafPEM := string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: leafDER}))

	// newLog serves the pinned log: get-sth, and get-entries for a
	// single-index range.
	newLog := func() *stubLog {
		s := newStubLog()
		s.json("/ct/v1/get-sth", 200, sthJSON)
		s.handle("/ct/v1/get-entries", func(w http.ResponseWriter, r *http.Request) {
			start := r.URL.Query().Get("start")
			idx := -1
			for i := 0; i < treeSize; i++ {
				if start == fmt.Sprintf("%d", i) {
					idx = i
				}
			}
			w.Header().Set("Content-Type", "application/json")
			if idx < 0 {
				w.WriteHeader(400)
				_, _ = w.Write([]byte(`{"error":"out of range"}`))
				return
			}
			_, _ = fmt.Fprintf(w, `{"entries":[{"leaf_input":%q,"extra_data":%q}]}`, leafInput[idx], extra)
		})
		return s
	}

	// wantIndex is the documented answer: the first entry whose timestamp is
	// at or after target, or treeSize when the log has none.
	wantIndex := func(target uint64) int {
		return sort.Search(treeSize, func(i int) bool { return entryTS(i) >= target })
	}

	// maxProbes is what "binary-searches ... one index per probe" costs at
	// this tree size: ceil(log2(8)) = 3 decisions, one re-read of the entry
	// the tool reports, and one more for a search that brackets the range
	// differently. A linear scan needs six probes to reach index 5 and eight
	// to run off the end, so it does not fit.
	maxProbes := 2
	for n := treeSize; n > 1; n = (n + 1) / 2 {
		maxProbes++
	}

	// checkProbes grades a server's request log against that shape. The order
	// of the probes is not compared: no document fixes it, and any correct
	// binary search is accepted.
	checkProbes := func(name string, reqs []string, want int, reported bool) {
		probes, err := probeIndices(reqs)
		ok := err == nil && len(probes) > 0 && len(probes) <= maxProbes
		if ok && reported {
			ok = false
			for _, i := range probes {
				if i == want {
					ok = true
				}
			}
		}
		t.Check(name, ok, "probes=%v err=%v (want 1..%d probes inside the tree, reported index %d present=%t)",
			probes, err, maxProbes, want, reported)
	}

	found := func(target uint64, idx int) string {
		return fmt.Sprintf("First entry with timestamp>=%d (%s) found at index %d\n",
			target, renderTime(target), idx) +
			fmt.Sprintf("Index=%d Timestamp=%d (%s) X.509 certificate:\n",
				idx, entryTS(idx), renderTime(entryTS(idx))) + leafPEM
	}

	// 1. A target that is exactly an entry's timestamp.
	target := entryTS(5)
	idx := wantIndex(target)
	s := newLog()
	defer s.close()
	r := runClient("bisect", "--log_uri="+s.URL, fmt.Sprintf("--timestamp=%d", target), "--text=false")
	t.Check("exact-hit-exit-status", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("exact-hit-stdout", r.stdout, found(target, idx))
	checkProbes("exact-hit-requests", s.requests(), idx, true)

	// 2. A target that falls between two entries resolves to the next one.
	target2 := entryTS(4) + 1
	idx2 := wantIndex(target2)
	s2 := newLog()
	defer s2.close()
	r2 := runClient("bisect", "--log_uri="+s2.URL, fmt.Sprintf("--timestamp=%d", target2), "--text=false")
	t.Equal("between-entries-stdout", r2.stdout, found(target2, idx2))
	checkProbes("between-entries-requests", s2.requests(), idx2, true)

	// 3. A target past the end of the log reports that and renders nothing.
	target3 := entryTS(treeSize-1) + 1
	s3 := newLog()
	defer s3.close()
	r3 := runClient("bisect", "--log_uri="+s3.URL, fmt.Sprintf("--timestamp=%d", target3), "--text=false")
	t.Check("past-end-exit-status", r3.code == 0, "exit=%d stderr=%s", r3.code, oneLine(r3.stderr))
	t.Equal("past-end-stdout", r3.stdout,
		fmt.Sprintf("No entry with timestamp>=%d (%s) found up to tree size %d\n",
			target3, renderTime(target3), treeSize))
	checkProbes("past-end-requests", s3.requests(), treeSize, false)

	// 4. The `find-timestamp` alias is the same command.
	s4 := newLog()
	defer s4.close()
	r4 := runClient("find-timestamp", "--log_uri="+s4.URL,
		fmt.Sprintf("--timestamp=%d", target), "--text=false")
	t.Equal("alias-find-timestamp-stdout", r4.stdout, found(target, idx))

	// 5. No --timestamp is fatal, before any request is made.
	s5 := newLog()
	defer s5.close()
	r5 := runClient("bisect", "--log_uri="+s5.URL, "--text=false")
	t.Check("missing-timestamp-is-fatal",
		r5.code != 0 && r5.stdout == "" && len(s5.requests()) == 0 &&
			strings.TrimSpace(r5.stderr) != "",
		"exit=%d requests=%v stderr=%s", r5.code, s5.requests(), oneLine(r5.stderr))

	t.Done()
}

// probeIndices reduces a stub log's request list to the entry indices the tool
// asked for, rejecting anything that is not the documented shape: an STH
// request first, then get-entries requests that each name exactly one index
// inside the tree. Query parameters are read by name, so their order does not
// matter here.
func probeIndices(reqs []string) ([]int, error) {
	if len(reqs) == 0 {
		return nil, fmt.Errorf("no requests at all")
	}
	if reqs[0] != "GET /ct/v1/get-sth" {
		return nil, fmt.Errorf("first request is %q, want GET /ct/v1/get-sth", reqs[0])
	}
	var idx []int
	for _, req := range reqs[1:] {
		u, err := url.Parse(strings.TrimPrefix(req, "GET "))
		if err != nil || u.Path != "/ct/v1/get-entries" {
			return idx, fmt.Errorf("request %q is not a get-entries request", req)
		}
		q := u.Query()
		start, errS := strconv.Atoi(q.Get("start"))
		end, errE := strconv.Atoi(q.Get("end"))
		if errS != nil || errE != nil || start != end {
			return idx, fmt.Errorf("request %q does not name exactly one index", req)
		}
		if start < 0 || start >= treeSize {
			return idx, fmt.Errorf("request %q probes index %d, outside a tree of %d", req, start, treeSize)
		}
		idx = append(idx, start)
	}
	return idx, nil
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}
