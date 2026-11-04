// backendstub.go is copied verbatim into every step-10 group that needs a log
// backend. It is not a group: it holds no cases and prints nothing. Each copy
// is compiled into exactly one group's single compilation unit, so a group
// still owns one process tree and one binary.
//
// SCOPE-BACKEND: google/trillian is not a deliverable and nothing below is an
// assertion about it. The stub exists because the capability under test needs
// a live backend to be visible at all: "read-only" means the write endpoints
// are gone WHILE the read endpoints still answer, and the second half of that
// sentence is unobservable against a dead backend. It is an in-process
// TrillianLogServer served over a real loopback gRPC socket on a port obtained
// by binding :0, written against this step's pinned trillian v1.4.1 API only.
// It is cheap because the CTFE performs no signature check on the log root it
// is handed -- from v1.1.0 the handlers call UnmarshalBinary on GetLogRoot()
// and stop, and the SCT and the STH are signed by the CTFE's own configured
// key -- so a marshalled unsigned LogRootV1 suffices and no log-root signing
// is reimplemented here.
//
// SCOPE-NET: loopback only. SCOPE-TIME: every timestamp, root hash and leaf
// below is a pinned constant.
package main

import (
	"context"
	"crypto/sha256"
	"net"
	"sync"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/trillian"
	"github.com/google/trillian/types"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	"ctverify/vhelp"
)

// Pinned backend state. Nothing here reads a clock.
const (
	pinnedLogID       = int64(1001)
	logPrefix         = "bench"
	pinnedTreeSize    = uint64(4321)
	pinnedTimestampMS = uint64(1500000000000) // 2017-07-14T02:40:00Z
)

// pinnedRootHash is a fixed 32-byte value. It is a hash only so that it is
// obviously not a real Merkle root taken from anywhere.
var pinnedRootHash = sha256.Sum256([]byte("harbor-ct-bench-step10-root"))

// pinnedExtraData is a TLS-encoded empty ct.CertificateChain.
var pinnedExtraData = []byte{0x00, 0x00, 0x00}

type stubBackend struct {
	mu      sync.Mutex
	calls   map[string]int
	leafDER []byte

	lastStart  int64
	lastCount  int64
	lastQueued []byte
}

func newStubBackend(leafDER []byte) *stubBackend {
	return &stubBackend{calls: map[string]int{}, leafDER: leafDER}
}

func (s *stubBackend) note(name string) {
	s.mu.Lock()
	s.calls[name]++
	s.mu.Unlock()
}

// count reports how many times the backend was asked for an RPC.
func (s *stubBackend) count(name string) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.calls[name]
}

func (s *stubBackend) rangeArgs() (start, count int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.lastStart, s.lastCount
}

func (s *stubBackend) queuedLeaf() []byte {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]byte(nil), s.lastQueued...)
}

func (s *stubBackend) signedRoot() *trillian.SignedLogRoot {
	r := types.LogRootV1{
		TreeSize:       pinnedTreeSize,
		RootHash:       pinnedRootHash[:],
		TimestampNanos: pinnedTimestampMS * 1000000,
	}
	b, err := r.MarshalBinary()
	if err != nil {
		panic(err)
	}
	return &trillian.SignedLogRoot{LogRoot: b}
}

// pinnedLeafValue is a well-formed RFC 6962 MerkleTreeLeaf for index i. Its
// bytes depend only on i and on a pinned fixture, so a driver can predict
// them exactly.
func pinnedLeafValue(der []byte, i int64) []byte {
	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS + uint64(i),
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: der},
		},
	}
	b, err := tls.Marshal(mtl)
	if err != nil {
		panic(err)
	}
	return b
}

func (s *stubBackend) leaf(i int64) *trillian.LogLeaf {
	return &trillian.LogLeaf{
		LeafIndex: i,
		LeafValue: pinnedLeafValue(s.leafDER, i),
		ExtraData: pinnedExtraData,
	}
}

func (s *stubBackend) QueueLeaf(_ context.Context, req *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	s.mu.Lock()
	s.calls["QueueLeaf"]++
	s.lastQueued = append([]byte(nil), req.GetLeaf().GetLeafValue()...)
	s.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) GetLeavesByRange(_ context.Context, req *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	s.mu.Lock()
	s.calls["GetLeavesByRange"]++
	s.lastStart = req.GetStartIndex()
	s.lastCount = req.GetCount()
	s.mu.Unlock()

	n := req.GetCount()
	if n < 0 {
		n = 0
	}
	if n > 1000 {
		n = 1000
	}
	leaves := make([]*trillian.LogLeaf, 0, n)
	for i := int64(0); i < n; i++ {
		leaves = append(leaves, s.leaf(req.GetStartIndex()+i))
	}
	return &trillian.GetLeavesByRangeResponse{Leaves: leaves, SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	s.note("GetLatestSignedLogRoot")
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetInclusionProof(_ context.Context, req *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	s.note("GetInclusionProof")
	return &trillian.GetInclusionProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetInclusionProofByHash(_ context.Context, _ *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	s.note("GetInclusionProofByHash")
	return &trillian.GetInclusionProofByHashResponse{
		Proof:         []*trillian.Proof{{LeafIndex: 7, Hashes: [][]byte{pinnedRootHash[:]}}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetConsistencyProof(_ context.Context, _ *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	s.note("GetConsistencyProof")
	return &trillian.GetConsistencyProofResponse{
		Proof:         &trillian.Proof{Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetEntryAndProof(_ context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	s.note("GetEntryAndProof")
	return &trillian.GetEntryAndProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		Leaf:          s.leaf(req.GetLeafIndex()),
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) InitLog(_ context.Context, _ *trillian.InitLogRequest) (*trillian.InitLogResponse, error) {
	s.note("InitLog")
	return &trillian.InitLogResponse{Created: s.signedRoot()}, nil
}

func (s *stubBackend) AddSequencedLeaves(_ context.Context, _ *trillian.AddSequencedLeavesRequest) (*trillian.AddSequencedLeavesResponse, error) {
	s.note("AddSequencedLeaves")
	return &trillian.AddSequencedLeavesResponse{}, nil
}

// serveBackend puts the stub on a real loopback gRPC socket and returns a
// client for it plus a shutdown function. The port comes from binding :0 and
// the listener and server are the only ones this group ever creates, so the
// teardown needs no process kill.
func serveBackend(t *vhelp.T, s *stubBackend) (trillian.TrillianLogClient, func()) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, s)
	go func() { _ = gs.Serve(ln) }()

	conn, err := grpc.Dial(ln.Addr().String(),
		grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		gs.Stop()
		t.Fatalf("dial backend: %v", err)
	}
	return trillian.NewTrillianLogClient(conn), func() {
		_ = conn.Close()
		gs.Stop()
	}
}
