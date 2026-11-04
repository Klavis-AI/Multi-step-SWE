// Group getentries-alignment-disabled.
//
// The other half of the get-entries coercion contract: --align_getentries=false
// must switch the truncation off and restore the pre-milestone shape, in which
// an over-long range is clipped to the maximum batch size and nothing else.
// The flag itself is new at v1.1.2, so v1.1.0's ct_server rejects it outright.
//
// Same boundary as the coercion group, same pinned grid, opposite expectation.
package main

import (
	"encoding/json"
	"fmt"
	"net/http"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const maxBatch = 16

func main() {
	t := vhelp.New()

	h := startPersonality(t, []string{
		fmt.Sprintf("--max_get_entries=%d", maxBatch),
		"--align_getentries=false",
	}, "")
	defer h.stop()

	type rangeCase struct {
		id         string
		start, end int64
		wantStart  int64
		wantCount  int64
	}
	cases := []rangeCase{
		// With alignment off a full batch off the grid is served whole.
		{"misaligned-full-batch-kept", 5, 20, 5, 16},
		// An over-long range is still clipped to the maximum, but not truncated.
		{"oversized-clipped-not-truncated", 5, 1000, 5, 16},
		// On-grid requests are unaffected either way.
		{"aligned-full-batch", 0, 15, 0, 16},
		// Short requests are unaffected either way.
		{"short-request-untouched", 7, 11, 7, 5},
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

	t.Done()
}
