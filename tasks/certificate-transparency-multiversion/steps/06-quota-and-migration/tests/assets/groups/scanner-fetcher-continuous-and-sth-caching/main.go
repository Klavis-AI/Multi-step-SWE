// Group scanner-fetcher-continuous-and-sth-caching.
//
// v1.0.19 rebuilds scanner.Fetcher for migrillian's keep-up mode. Three
// observable changes:
//
//   - FetcherOptions.Quiet and Fetcher.Log are gone, replaced by glog, and a
//     new Continuous option says "do not stop at EndIndex";
//   - Prepare now caches the STH and returns the cached one on a second call
//     instead of re-querying the log;
//   - with Continuous set, reaching the end of the range makes the Fetcher
//     re-read the STH and extend EndIndex to the new tree size, so a fetch
//     that started against a tree of N entries continues into entries the log
//     only published afterwards.
//
// Graded over a real socket: the driver runs its own CT log on loopback (port
// from binding :0) serving get-sth and get-entries from constants, and records
// every request. The continuous case is driven by a log whose tree grows from
// 4 to 8 entries after the first get-sth, and the run is ended by cancelling
// the context once the callback has seen the eighth entry -- so the group
// asserts what was fetched, never how long anything took.
//
// SCOPE-NET: loopback only, on a port obtained by binding :0.
// SCOPE-TIME: every served timestamp is a constant; no clock is asserted on.
//
// FetcherOptions has no Continuous field at v1.0.17, so this driver does not
// compile against the previous tree.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"strings"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/scanner"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	// Entry i is served with this timestamp plus i*1000, so the driver can
	// name every entry it was handed exactly.
	baseTimestamp = uint64(1519737600000)
	stepPerEntry  = uint64(1000)

	// A well-formed DigitallySigned; the client is created without a public
	// key, so the STH signature is decoded but not verified.
	pinnedTreeHeadSigB64 = "BAMARgABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AQUJDREU="
	pinnedRootHashB64    = "8HkFQvkB1SGf0EjbNzZJsY1eqyaUJSngYMrFbwXi+cU="
)

// fakeLog serves get-sth and get-entries from constants and records what it
// was asked. treeSizes supplies the tree size for successive get-sth calls;
// the last value is repeated once the slice is exhausted.
type fakeLog struct {
	mu        sync.Mutex
	treeSizes []uint64
	sthCalls  int
	queries   []string
	fail      bool

	ln      net.Listener
	srv     *http.Server
	baseURI string
	leafDER []byte
}

func newFakeLog(t *vhelp.T, leafDER []byte, fail bool, treeSizes ...uint64) *fakeLog {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	f := &fakeLog{treeSizes: treeSizes, fail: fail, ln: ln,
		baseURI: "http://" + ln.Addr().String(), leafDER: leafDER}
	mux := http.NewServeMux()
	mux.HandleFunc(ct.GetSTHPath, f.serveSTH)
	mux.HandleFunc(ct.GetEntriesPath, f.serveEntries)
	f.srv = &http.Server{Handler: mux}
	go f.srv.Serve(ln)
	return f
}

func (f *fakeLog) serveSTH(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	f.sthCalls++
	if f.fail {
		f.mu.Unlock()
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(`{"error":"log is down"}`))
		return
	}
	idx := f.sthCalls - 1
	if idx >= len(f.treeSizes) {
		idx = len(f.treeSizes) - 1
	}
	size := f.treeSizes[idx]
	f.mu.Unlock()

	body, _ := json.Marshal(map[string]interface{}{
		"tree_size":           size,
		"timestamp":           baseTimestamp,
		"sha256_root_hash":    pinnedRootHashB64,
		"tree_head_signature": pinnedTreeHeadSigB64,
	})
	w.Header().Set("Content-Type", "application/json")
	w.Write(body)
}

// leafAt builds the entry served at a given index: a real RFC 6962 leaf whose
// timestamp names the index.
func (f *fakeLog) leafAt(i uint64) []byte {
	b, err := tls.Marshal(ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: baseTimestamp + i*stepPerEntry,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: f.leafDER},
		},
	})
	if err != nil {
		return nil
	}
	return b
}

func (f *fakeLog) serveEntries(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	f.queries = append(f.queries, r.URL.RawQuery)
	f.mu.Unlock()

	q := r.URL.Query()
	var start, end uint64
	fmt.Sscanf(q.Get("start"), "%d", &start)
	fmt.Sscanf(q.Get("end"), "%d", &end)

	rsp := ct.GetEntriesResponse{}
	for i := start; i <= end; i++ {
		rsp.Entries = append(rsp.Entries, ct.LeafEntry{LeafInput: f.leafAt(i), ExtraData: []byte{}})
	}
	body, _ := json.Marshal(rsp)
	w.Header().Set("Content-Type", "application/json")
	w.Write(body)
}

func (f *fakeLog) stats() (int, []string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.sthCalls, append([]string{}, f.queries...)
}

func (f *fakeLog) stop() {
	f.srv.Close()
	f.ln.Close()
}

func (f *fakeLog) fetcher(t *vhelp.T, opts *scanner.FetcherOptions) *scanner.Fetcher {
	lc, err := client.New(f.baseURI, nil, jsonclient.Options{})
	if err != nil {
		t.Fatalf("client.New: %v", err)
	}
	return scanner.NewFetcher(lc, opts)
}

// indexOf recovers the entry index the fake log encoded in a leaf.
func indexOf(b []byte) string {
	var leaf ct.MerkleTreeLeaf
	rest, err := tls.Unmarshal(b, &leaf)
	if err != nil || len(rest) != 0 || leaf.TimestampedEntry == nil {
		return "?"
	}
	return fmt.Sprintf("%d", (leaf.TimestampedEntry.Timestamp-baseTimestamp)/stepPerEntry)
}

func main() {
	t := vhelp.New()
	ctx := context.Background()
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")

	// --- 1. the defaults say "stop at the end" ------------------------------
	d := scanner.DefaultFetcherOptions()
	t.Equal("default-fetcher-options-are-bounded",
		fmt.Sprintf("batch=%d parallel=%d start=%d end=%d continuous=%v",
			d.BatchSize, d.ParallelFetch, d.StartIndex, d.EndIndex, d.Continuous),
		"batch=1000 parallel=1 start=0 end=0 continuous=false")

	// --- 2. Prepare caches the STH instead of re-querying --------------------
	cache := newFakeLog(t, leafDER, false, 10)
	defer cache.stop()
	cf := cache.fetcher(t, &scanner.FetcherOptions{BatchSize: 4, ParallelFetch: 1})
	first, err1 := cf.Prepare(ctx)
	second, err2 := cf.Prepare(ctx)
	calls, _ := cache.stats()
	render := func(s *ct.SignedTreeHead) string {
		if s == nil {
			return "nil"
		}
		return fmt.Sprintf("size=%d ts=%d", s.TreeSize, s.Timestamp)
	}
	t.Equal("prepare-caches-the-sth-for-the-second-call",
		fmt.Sprintf("getSTHRequests=%d err=%v,%v first=%s second=%s",
			calls, err1, err2, render(first), render(second)),
		fmt.Sprintf("getSTHRequests=%d err=%v,%v first=size=10 ts=%d second=size=10 ts=%d",
			1, error(nil), error(nil), baseTimestamp, baseTimestamp))

	// --- 3. and clamps the range to the tree it found -----------------------
	clamp := newFakeLog(t, leafDER, false, 10)
	defer clamp.stop()
	var ends []string
	for _, want := range []int64{0, 25, 6} {
		opts := &scanner.FetcherOptions{BatchSize: 4, ParallelFetch: 1, EndIndex: want}
		if _, err := clamp.fetcher(t, opts).Prepare(ctx); err != nil {
			t.Fatalf("Prepare(EndIndex=%d): %v", want, err)
		}
		ends = append(ends, fmt.Sprintf("%d->%d", want, opts.EndIndex))
	}
	t.Equal("prepare-clamps-end-index-to-the-tree-size",
		strings.Join(ends, " "), "0->10 25->10 6->6")

	// --- 4. a log that will not answer is reported --------------------------
	down := newFakeLog(t, leafDER, true, 10)
	defer down.stop()
	downSTH, downErr := down.fetcher(t, &scanner.FetcherOptions{BatchSize: 4, ParallelFetch: 1}).Prepare(ctx)
	t.Equal("prepare-reports-a-log-that-will-not-answer",
		fmt.Sprintf("sth=%v err=%v", downSTH == nil, downErr != nil), "sth=true err=true")

	// --- 5. a bounded run fetches exactly its configured range --------------
	bounded := newFakeLog(t, leafDER, false, 10)
	defer bounded.stop()
	bf := bounded.fetcher(t, &scanner.FetcherOptions{
		BatchSize: 3, ParallelFetch: 1, StartIndex: 2, EndIndex: 7, Continuous: false})
	var seen []string
	if err := bf.Run(ctx, func(b scanner.EntryBatch) {
		for _, e := range b.Entries {
			seen = append(seen, indexOf(e.LeafInput))
		}
	}); err != nil {
		t.Fatalf("bounded Run: %v", err)
	}
	_, boundedQueries := bounded.stats()
	t.Equal("bounded-run-fetches-exactly-the-configured-range",
		fmt.Sprintf("entries=[%s] queries=%v", strings.Join(seen, " "), boundedQueries),
		"entries=[2 3 4 5 6] queries=[end=4&start=2 end=6&start=5]")

	// --- 6. a continuous run follows the tree as it grows -------------------
	// The log reports 4 entries to the first get-sth and 8 to every later one.
	// A bounded Fetcher would stop after entry 3; a continuous one re-reads
	// the STH at the end of the range and carries on to entry 7.
	grow := newFakeLog(t, leafDER, false, 4, 8)
	defer grow.stop()
	gctx, cancel := context.WithCancel(ctx)
	defer cancel()
	gf := grow.fetcher(t, &scanner.FetcherOptions{
		BatchSize: 4, ParallelFetch: 1, StartIndex: 0, EndIndex: 0, Continuous: true})
	var grown []string
	var gmu sync.Mutex
	gerr := gf.Run(gctx, func(b scanner.EntryBatch) {
		gmu.Lock()
		for _, e := range b.Entries {
			grown = append(grown, indexOf(e.LeafInput))
		}
		done := len(grown) >= 8
		gmu.Unlock()
		if done {
			cancel()
		}
	})
	// CLIENT-API.md 5.1 says a continuous run is ended by cancelling the
	// context it was given, but does not fix what Run returns for that ending.
	// Both a nil return and the context's own error are therefore accepted;
	// only some other error means the fetch itself failed. What this group
	// grades is the batches that were delivered and the queries that produced
	// them, asserted below.
	if gerr != nil && !errors.Is(gerr, context.Canceled) && !errors.Is(gerr, context.DeadlineExceeded) {
		t.Fatalf("continuous Run: %v", gerr)
	}
	_, growQueries := grow.stats()
	t.Equal("continuous-run-extends-the-range-as-the-tree-grows",
		fmt.Sprintf("entries=[%s] queries=%v", strings.Join(grown, " "), growQueries),
		"entries=[0 1 2 3 4 5 6 7] queries=[end=3&start=0 end=7&start=4]")

	t.Done()
}
