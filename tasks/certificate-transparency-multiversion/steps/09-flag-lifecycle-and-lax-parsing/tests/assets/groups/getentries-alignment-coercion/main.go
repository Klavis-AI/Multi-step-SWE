// Group getentries-alignment-coercion.
//
// v1.1.2 coerces get-entries callers onto a shared cache-friendly grid: a
// request whose entry count reaches the maximum batch size has its response
// truncated at the next multiple of that maximum. The boundary is the RFC 6962
// get-entries endpoint of the candidate's own ct_server, and the observable is
// both the number of entries returned and the [start, count) the CTFE asked
// the pinned backend for.
//
// --max_get_entries=16 pins the grid, so every expected value below is a
// constant. v1.1.0 clips over-long ranges but never truncates to the grid.
//
// Case 7 is inherited rather than new: it was the gate's
// `get-entries-exact-leaves` and moved here at the 2026-09-21 re-tier, because
// a gate group must compile against v1.1.0 too and the pinned trillian module
// -- and so _kit/serverkit.go -- does not survive that anchor. It lands here
// because this is the scored group that already reads get-entries responses
// off the pinned backend; it adds the one thing the six cases above do not
// look at, the bytes of the entries themselves. It cannot make this group
// satisfiable by inherited work alone: cases 1 to 6 are the v1.1.2 coercion.
package main

import (
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const maxBatch = 16

type rangeCase struct {
	id         string
	start, end int64
	wantStart  int64
	wantCount  int64
}

func main() {
	t := vhelp.New()

	h := startPersonality(t, []string{fmt.Sprintf("--max_get_entries=%d", maxBatch)}, "")
	defer h.stop()

	cases := []rangeCase{
		// Exactly on the grid already: nothing to truncate.
		{"aligned-full-batch", 0, 15, 0, 16},
		// A full batch that starts off the grid: [5,20] becomes [5,15].
		{"misaligned-full-batch-truncated", 5, 20, 5, 11},
		// Over-long and off the grid: clipped to [5,20], then truncated to [5,15].
		{"oversized-clipped-then-truncated", 5, 1000, 5, 11},
		// Over-long but landing on the grid once clipped: [32,47] survives whole.
		{"oversized-clipped-stays-aligned", 32, 1000, 32, 16},
		// Below the maximum: untouched, whatever the alignment.
		{"short-request-untouched", 5, 9, 5, 5},
		{"single-entry-untouched", 100, 100, 100, 1},
	}

	for _, c := range cases {
		url := fmt.Sprintf("%s?start=%d&end=%d", h.url(ct.GetEntriesPath), c.start, c.end)
		res := h.get(t, url)
		if res.status != http.StatusOK {
			t.Check(c.id, false, "status %d", res.status)
			continue
		}
		var rsp ct.GetEntriesResponse
		if err := json.Unmarshal(res.body, &rsp); err != nil {
			t.Check(c.id, false, "unmarshal: %v", err)
			continue
		}
		_, gotStart, gotCount := h.backend.snapshot()
		t.Check(c.id,
			int64(len(rsp.Entries)) == c.wantCount && gotStart == c.wantStart && gotCount == c.wantCount,
			"returned %d entries and asked the backend for start=%d count=%d; want %d entries and start=%d count=%d",
			len(rsp.Entries), gotStart, gotCount, c.wantCount, c.wantStart, c.wantCount)
	}

	// 7. The entries themselves, byte for byte, over a short range the
	// coercion leaves alone: each leaf_input is the exact RFC 6962
	// MerkleTreeLeaf the pinned backend holds for that index and each
	// extra_data is its pinned three bytes. Inherited obligation, see header.
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	const exactStart, exactEnd = 5, 9
	res := h.get(t, fmt.Sprintf("%s?start=%d&end=%d", h.url(ct.GetEntriesPath), exactStart, exactEnd))
	var exact ct.GetEntriesResponse
	if res.status != http.StatusOK {
		t.Check("get-entries-exact-leaves", false, "status %d", res.status)
	} else if err := json.Unmarshal(res.body, &exact); err != nil {
		t.Check("get-entries-exact-leaves", false, "unmarshal: %v", err)
	} else if len(exact.Entries) != exactEnd-exactStart+1 {
		t.Check("get-entries-exact-leaves", false, "got %d entries, want %d",
			len(exact.Entries), exactEnd-exactStart+1)
	} else {
		bad := ""
		for i, e := range exact.Entries {
			want := pinnedLeafValue(leafDER, int64(exactStart+i))
			if hex.EncodeToString(e.LeafInput) != hex.EncodeToString(want) {
				bad += fmt.Sprintf("entry %d leaf_input mismatch; ", i)
			}
			if hex.EncodeToString(e.ExtraData) != "000000" {
				bad += fmt.Sprintf("entry %d extra_data=%x; ", i, e.ExtraData)
			}
		}
		t.Check("get-entries-exact-leaves", bad == "", "%s", bad)
	}

	t.Done()
}
