// Group scanner-fetcher-api.
//
// v1.0.17 lifts the entry-retrieval half of the scanner out into a reusable
// component (commit 83a60276, "Extract Fetcher from Scanner"): scanner/
// gains fetcher.go with FetcherOptions, DefaultFetcherOptions, Fetcher,
// NewFetcher, EntryBatch, Prepare and Run, and ScannerOptions is rebuilt to
// embed FetcherOptions rather than repeat its fields. The genuinely new
// knob is EndIndex -- v1.0.13's scanner could say where to start and never
// where to stop.
//
// Graded by being the log. A Fetcher is defined by what it asks the log for
// and what it hands back, so the driver serves RFC 6962's two read endpoints
// on a loopback socket from pinned constants (see fakelog.go) and asserts
// both the entries delivered and the ranges requested.
//
// SCOPE-NET: 127.0.0.1, port from binding :0. SCOPE-TIME: entry timestamps
// are constants; each entry's timestamp carries its own index so the delivery
// order is checkable without a clock.
//
// v1.0.13 has no scanner/fetcher.go at all, so this driver does not compile
// there.
package main

import (
	"context"
	"fmt"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/scanner"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// treeSize is the number of entries the loopback log serves.
const treeSize = 12

// buildEntries mints treeSize distinct leaves over one pinned certificate.
// Entry i's MerkleTreeLeaf timestamp is fakeSTHTimestamp+i, which is how the
// assertions below recover an entry's index from its bytes alone.
func buildEntries(t *vhelp.T) []fakeEntry {
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	out := make([]fakeEntry, treeSize)
	for i := 0; i < treeSize; i++ {
		leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, fakeSTHTimestamp+uint64(i))
		b, err := tls.Marshal(*leaf)
		if err != nil {
			t.Fatalf("marshal leaf %d: %v", i, err)
		}
		out[i] = fakeEntry{LeafInput: b, ExtraData: []byte{}}
	}
	return out
}

// indexOf recovers the index encoded in an entry's leaf timestamp.
func indexOf(t *vhelp.T, e ct.LeafEntry) int64 {
	var leaf ct.MerkleTreeLeaf
	if _, err := tls.Unmarshal(e.LeafInput, &leaf); err != nil {
		t.Fatalf("unmarshal delivered leaf: %v", err)
	}
	return int64(leaf.TimestampedEntry.Timestamp - fakeSTHTimestamp)
}

// collect runs a Fetcher over the log and returns the delivered indices in
// delivery order together with the batch start offsets.
func collect(t *vhelp.T, f *scanner.Fetcher) ([]int64, []int64) {
	var mu sync.Mutex
	var idx, starts []int64
	if err := f.Run(context.Background(), func(b scanner.EntryBatch) {
		mu.Lock()
		defer mu.Unlock()
		starts = append(starts, b.Start)
		for _, e := range b.Entries {
			idx = append(idx, indexOf(t, e))
		}
	}); err != nil {
		t.Fatalf("Fetcher.Run: %v", err)
	}
	return idx, starts
}

func main() {
	t := vhelp.New()

	log, err := startFakeLog(buildEntries(t), 5)
	if err != nil {
		t.Fatalf("start loopback log: %v", err)
	}
	defer log.Close()

	newClient := func() *client.LogClient {
		lc, err := client.New(log.URL, nil, jsonclient.Options{})
		if err != nil {
			t.Fatalf("client.New: %v", err)
		}
		return lc
	}

	// --- 1. the documented defaults ----------------------------------------
	d := scanner.DefaultFetcherOptions()
	t.Equal("default-fetcher-options",
		fmt.Sprintf("batch=%d parallel=%d start=%d end=%d quiet=%v",
			d.BatchSize, d.ParallelFetch, d.StartIndex, d.EndIndex, d.Quiet),
		"batch=1000 parallel=1 start=0 end=0 quiet=false")

	// --- 2. Prepare caches the STH and fills in an unset EndIndex -----------
	opts := &scanner.FetcherOptions{BatchSize: 5, ParallelFetch: 1, StartIndex: 0, EndIndex: 0, Quiet: true}
	sth, err := scanner.NewFetcher(newClient(), opts).Prepare(context.Background())
	if err != nil {
		t.Check("prepare-clamps-unset-end-index-to-tree-size", false, "Prepare: %v", err)
	} else {
		t.Equal("prepare-clamps-unset-end-index-to-tree-size",
			fmt.Sprintf("treesize=%d end=%d", sth.TreeSize, opts.EndIndex),
			fmt.Sprintf("treesize=%d end=%d", treeSize, treeSize))
	}

	// --- 3. ... and cuts an oversized one down to the tree ------------------
	opts = &scanner.FetcherOptions{BatchSize: 5, ParallelFetch: 1, StartIndex: 0, EndIndex: 1000, Quiet: true}
	if _, err := scanner.NewFetcher(newClient(), opts).Prepare(context.Background()); err != nil {
		t.Check("prepare-clamps-oversized-end-index", false, "Prepare: %v", err)
	} else {
		t.Equal("prepare-clamps-oversized-end-index", opts.EndIndex, int64(treeSize))
	}

	// --- 4. a whole-log run delivers every entry once, in order -------------
	opts = &scanner.FetcherOptions{BatchSize: 5, ParallelFetch: 1, StartIndex: 0, EndIndex: 0, Quiet: true}
	idx, starts := collect(t, scanner.NewFetcher(newClient(), opts))
	t.Equal("run-delivers-every-entry-in-order",
		fmt.Sprintf("indices=%v starts=%v", idx, starts),
		"indices=[0 1 2 3 4 5 6 7 8 9 10 11] starts=[0 5 10]")

	// --- 5. EndIndex is exclusive and really bounds the traffic -------------
	// [2, 7) with a batch of 3 is two ranges, [2,4] and [5,6]; the second is
	// short because EndIndex cuts it, not because the tree ran out.
	before := len(log.Ranges())
	opts = &scanner.FetcherOptions{BatchSize: 3, ParallelFetch: 1, StartIndex: 2, EndIndex: 7, Quiet: true}
	idx, starts = collect(t, scanner.NewFetcher(newClient(), opts))
	asked := log.Ranges()[before:]
	t.Equal("end-index-bounds-the-fetch",
		fmt.Sprintf("indices=%v starts=%v asked=%v", idx, starts, asked),
		"indices=[2 3 4 5 6] starts=[2 5] asked=[[2 4] [5 6]]")

	// --- 6. ScannerOptions now embeds the fetcher's options -----------------
	// Reached through the embedded struct and through promotion, which is what
	// makes the two configurations one configuration.
	so := scanner.DefaultScannerOptions()
	so.FetcherOptions.EndIndex = 9
	t.Equal("scanner-options-embeds-fetcher-options",
		fmt.Sprintf("batch=%d parallel=%d end=%d workers=%d precertonly=%v",
			so.BatchSize, so.ParallelFetch, so.EndIndex, so.NumWorkers, so.PrecertOnly),
		"batch=1000 parallel=1 end=9 workers=1 precertonly=false")

	t.Done()
}
