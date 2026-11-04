// consistencylog.go is the other end of the client this group drives. It is
// not a group: it holds no cases, prints nothing, and is compiled into this
// group's single compilation unit.
//
// It serves exactly one RFC 6962 read endpoint, get-sth-consistency, over a
// loopback socket, answering from a Merkle tree built here from the three
// hashing rules of MERKLE.md section 1. Serving a *correct* proof is the whole
// point: when the candidate's ctclient then refuses it, the refusal can only
// come from the candidate's verifier, and when the driver deliberately damages
// a root or an element, an acceptance can only come from there too.
//
// Stdlib only, so it compiles unchanged at every anchor.
//
// SCOPE-NET: 127.0.0.1 only, port obtained by binding :0, no name resolution.
// SCOPE-TIME: nothing here reads a clock.
package main

import (
	"crypto/sha256"
	"encoding/json"
	"net"
	"net/http"
	"strconv"
)

// --- RFC 6962 section 2.1, implemented from MERKLE.md section 1 -------------

func hashLeaf(data []byte) []byte {
	h := sha256.Sum256(append([]byte{0x00}, data...))
	return h[:]
}

func hashNode(l, r []byte) []byte {
	b := make([]byte, 0, 1+len(l)+len(r))
	b = append(b, 0x01)
	b = append(b, l...)
	b = append(b, r...)
	h := sha256.Sum256(b)
	return h[:]
}

// split is RFC 6962's k: the largest power of two strictly less than n.
func split(n int) int {
	k := 1
	for k<<1 < n {
		k <<= 1
	}
	return k
}

// mth is the Merkle tree hash of a list of leaf hashes.
func mth(leaves [][]byte) []byte {
	switch len(leaves) {
	case 0:
		h := sha256.Sum256(nil)
		return h[:]
	case 1:
		return leaves[0]
	}
	k := split(len(leaves))
	return hashNode(mth(leaves[:k]), mth(leaves[k:]))
}

// subProof is RFC 6962 section 2.1.2's SUBPROOF(m, D[n], b).
func subProof(m int, leaves [][]byte, b bool) [][]byte {
	if m == len(leaves) {
		if b {
			return nil
		}
		return [][]byte{mth(leaves)}
	}
	k := split(len(leaves))
	if m <= k {
		return append(subProof(m, leaves[:k], b), mth(leaves[k:]))
	}
	return append(subProof(m-k, leaves[k:], false), mth(leaves[:k]))
}

// consistencyProof is PROOF(m, D[n]) for 0 < m <= n.
func consistencyProof(m int, leaves [][]byte) [][]byte {
	if m <= 0 || m > len(leaves) {
		return nil
	}
	return subProof(m, leaves, true)
}

// --- the log ----------------------------------------------------------------

// consistencyLog answers get-sth-consistency over loopback.
type consistencyLog struct {
	URL string

	ln  net.Listener
	srv *http.Server

	leaves [][]byte

	// tamper, when set, rewrites a proof just before it is serialised.
	tamper func([][]byte) [][]byte
}

// root returns the Merkle tree hash at the given snapshot size.
func (c *consistencyLog) root(size int) []byte { return mth(c.leaves[:size]) }

// startConsistencyLog serves at http://127.0.0.1:<port>/ over a tree of the
// given leaf hashes.
func startConsistencyLog(leaves [][]byte, tamper func([][]byte) [][]byte) (*consistencyLog, error) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return nil, err
	}
	c := &consistencyLog{
		URL:    "http://" + ln.Addr().String(),
		ln:     ln,
		leaves: leaves,
		tamper: tamper,
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/ct/v1/get-sth-consistency", c.handleConsistency)
	c.srv = &http.Server{Handler: mux}
	go func() { _ = c.srv.Serve(ln) }()
	return c, nil
}

func (c *consistencyLog) Close() { _ = c.srv.Close() }

func (c *consistencyLog) handleConsistency(w http.ResponseWriter, r *http.Request) {
	first, err1 := strconv.Atoi(r.URL.Query().Get("first"))
	second, err2 := strconv.Atoi(r.URL.Query().Get("second"))
	if err1 != nil || err2 != nil {
		w.WriteHeader(http.StatusBadRequest)
		return
	}
	var proof [][]byte
	// A well-formed 200 is returned even for a range the tree cannot honour,
	// so that the *client's* verifier is what decides. An empty proof is the
	// honest answer for a range this log cannot prove anything about.
	if first > 0 && second <= len(c.leaves) && first <= second {
		proof = consistencyProof(first, c.leaves[:second])
	}
	if c.tamper != nil {
		proof = c.tamper(proof)
	}
	if proof == nil {
		proof = [][]byte{}
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(struct {
		Consistency [][]byte `json:"consistency"`
	}{proof})
}
