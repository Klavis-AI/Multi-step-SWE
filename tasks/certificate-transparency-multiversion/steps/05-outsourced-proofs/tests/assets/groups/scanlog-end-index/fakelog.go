// fakelog.go is copied verbatim into every group that needs something at the
// other end of a CT client. It is not a group: it holds no cases, prints
// nothing, and every copy is compiled into exactly one group's single
// compilation unit, so a group still owns one process and one binary.
//
// What it is: the read side of RFC 6962 -- get-sth and get-entries -- served
// over a real loopback HTTP socket, answering from constants handed to it by
// the driver. It exists because scanner.Fetcher and the scanlog binary are
// defined entirely by how they talk to a log, and there is no way to observe
// their batching, their range arithmetic or their EndIndex bound except by
// being the log and recording what was asked for.
//
// SCOPE-NET: 127.0.0.1 only, port obtained by binding :0, no name resolution.
// SCOPE-TIME: the STH timestamp and root hash are constants; nothing here
// reads a clock. SCOPE-BACKEND: this is not Trillian and asserts nothing
// about it -- the candidate's CTFE is not involved at all, only its client.
//
// Stdlib only, so it compiles unchanged at every anchor.
package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"strconv"
	"sync"
)

// fakeSTHTimestamp is the pinned SCT-style timestamp reported by get-sth
// (2018-02-27T13:20:00Z in milliseconds).
const fakeSTHTimestamp = uint64(1519737600000)

// fakeRootHash is a pinned 32-byte root hash. Its value is never asserted on;
// it only has to be the right length for the client to accept the STH.
var fakeRootHash = func() []byte {
	b := make([]byte, 32)
	for i := range b {
		b[i] = byte(0xA0 + i)
	}
	return b
}()

// fakeTreeHeadSignature is a well-formed but empty TLS DigitallySigned
// (SHA-256 / ECDSA, zero-length signature). The client unmarshals it and, with
// no public key configured, does not verify it.
var fakeTreeHeadSignature = []byte{4, 3, 0, 0}

// fakeEntry is one log entry as the wire carries it.
type fakeEntry struct {
	LeafInput []byte
	ExtraData []byte
}

// fakeLog is a running loopback CT log.
type fakeLog struct {
	URL string

	ln       net.Listener
	srv      *http.Server
	entries  []fakeEntry
	maxBatch int

	mu      sync.Mutex
	ranges  [][2]int64
	sthHits int
}

// startFakeLog serves entries at http://127.0.0.1:<port>/ with a tree size of
// len(entries). maxBatch caps how many entries one get-entries answer carries,
// which is what a real log's own limit does.
func startFakeLog(entries []fakeEntry, maxBatch int) (*fakeLog, error) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	f := &fakeLog{
		URL:      "http://" + ln.Addr().String(),
		ln:       ln,
		entries:  entries,
		maxBatch: maxBatch,
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/ct/v1/get-sth", f.handleSTH)
	mux.HandleFunc("/ct/v1/get-entries", f.handleEntries)
	f.srv = &http.Server{Handler: mux}
	go f.srv.Serve(ln)
	return f, nil
}

// Close stops the server. Safe to call twice.
func (f *fakeLog) Close() {
	f.srv.Close()
}

// Ranges returns the [start, end] pairs this log was asked for, in order.
func (f *fakeLog) Ranges() [][2]int64 {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([][2]int64, len(f.ranges))
	copy(out, f.ranges)
	return out
}

// STHHits returns how many times get-sth was called.
func (f *fakeLog) STHHits() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.sthHits
}

func (f *fakeLog) handleSTH(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	f.sthHits++
	f.mu.Unlock()
	writeJSON(w, map[string]interface{}{
		"tree_size":           len(f.entries),
		"timestamp":           fakeSTHTimestamp,
		"sha256_root_hash":    base64.StdEncoding.EncodeToString(fakeRootHash),
		"tree_head_signature": base64.StdEncoding.EncodeToString(fakeTreeHeadSignature),
	})
}

func (f *fakeLog) handleEntries(w http.ResponseWriter, r *http.Request) {
	start, err1 := strconv.ParseInt(r.URL.Query().Get("start"), 10, 64)
	end, err2 := strconv.ParseInt(r.URL.Query().Get("end"), 10, 64)
	if err1 != nil || err2 != nil || start < 0 || end < start {
		http.Error(w, `{"error_message":"bad range","success":false}`, http.StatusBadRequest)
		return
	}
	f.mu.Lock()
	f.ranges = append(f.ranges, [2]int64{start, end})
	f.mu.Unlock()

	if start >= int64(len(f.entries)) {
		http.Error(w, `{"error_message":"start beyond tree size","success":false}`, http.StatusBadRequest)
		return
	}
	last := end
	if last >= int64(len(f.entries)) {
		last = int64(len(f.entries)) - 1
	}
	if f.maxBatch > 0 && last-start+1 > int64(f.maxBatch) {
		last = start + int64(f.maxBatch) - 1
	}
	out := make([]map[string]string, 0, last-start+1)
	for i := start; i <= last; i++ {
		out = append(out, map[string]string{
			"leaf_input": base64.StdEncoding.EncodeToString(f.entries[i].LeafInput),
			"extra_data": base64.StdEncoding.EncodeToString(f.entries[i].ExtraData),
		})
	}
	writeJSON(w, map[string]interface{}{"entries": out})
}

func writeJSON(w http.ResponseWriter, v interface{}) {
	b, err := json.Marshal(v)
	if err != nil {
		http.Error(w, fmt.Sprintf(`{"error_message":%q,"success":false}`, err.Error()), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Write(b)
}
