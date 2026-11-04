package main

import (
	"encoding/binary"
	"sync"

	"github.com/google/trillian"
	"golang.org/x/net/context"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// stubLog is a pinned in-process TrillianLogServer for this step's Trillian
// pin (v0.0.0-20170911114049-c0a332878f82, whose SignedLogRoot still carries
// struct fields rather than a marshalled LogRootV1). It stores nothing and
// answers from constants only, so every value the personality derives from the
// backend is fixed. It is not a Trillian implementation and is not expected to
// be one; it exists so the CTFE's own code path can be walked end to end.
type stubLog struct {
	mu sync.Mutex

	// what the personality sent us, and what we sent back
	sentLeaves     [][]byte
	returnedLeaves [][]byte
	queueCalls     int

	// When set to anything but codes.OK, GetInclusionProofByHash answers with
	// that gRPC status instead of a proof. Used at the end of the run to walk
	// the backend-error map of HTTP-API.md section 6.1; every earlier case runs
	// with it unset.
	proofFailure codes.Code
}

func (s *stubLog) failProofsWith(c codes.Code) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.proofFailure = c
}

func (s *stubLog) snapshot() (sent, returned [][]byte, calls int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([][]byte{}, s.sentLeaves...), append([][]byte{}, s.returnedLeaves...), s.queueCalls
}

// rewriteTimestamp replaces the 8-byte timestamp inside a TLS-encoded
// MerkleTreeLeaf (version, leaf_type, timestamp, ...) with a pinned value, so
// the SCT the personality issues is fully determined and never reads a clock.
func rewriteTimestamp(leafValue []byte, ts uint64) []byte {
	out := append([]byte{}, leafValue...)
	if len(out) >= 10 {
		binary.BigEndian.PutUint64(out[2:10], ts)
	}
	return out
}

func (s *stubLog) QueueLeaves(ctx context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.queueCalls++
	rsp := &trillian.QueueLeavesResponse{}
	for _, in := range req.Leaves {
		s.sentLeaves = append(s.sentLeaves, append([]byte{}, in.LeafValue...))
		out := rewriteTimestamp(in.LeafValue, pinnedLeafTimestamp)
		s.returnedLeaves = append(s.returnedLeaves, out)
		rsp.QueuedLeaves = append(rsp.QueuedLeaves, &trillian.QueuedLogLeaf{
			Leaf: &trillian.LogLeaf{
				LeafValue:      out,
				ExtraData:      in.ExtraData,
				MerkleLeafHash: in.MerkleLeafHash,
				LeafIndex:      pinnedLeafIndex,
			},
		})
	}
	return rsp, nil
}

func (s *stubLog) GetLatestSignedLogRoot(ctx context.Context, req *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	return &trillian.GetLatestSignedLogRootResponse{
		SignedLogRoot: &trillian.SignedLogRoot{
			TimestampNanos: int64(pinnedSTHTimestampMillis) * 1000 * 1000,
			RootHash:       pinnedRootHash(),
			TreeSize:       pinnedTreeSize,
			LogId:          req.LogId,
			TreeRevision:   3,
		},
	}, nil
}

func (s *stubLog) GetLeavesByIndex(ctx context.Context, req *trillian.GetLeavesByIndexRequest) (*trillian.GetLeavesByIndexResponse, error) {
	rsp := &trillian.GetLeavesByIndexResponse{}
	for _, idx := range req.LeafIndex {
		rsp.Leaves = append(rsp.Leaves, &trillian.LogLeaf{
			LeafIndex: idx,
			LeafValue: pinnedStoredLeaf(),
			ExtraData: pinnedStoredExtraData(),
		})
	}
	return rsp, nil
}

func (s *stubLog) GetInclusionProofByHash(ctx context.Context, req *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	s.mu.Lock()
	fail := s.proofFailure
	s.mu.Unlock()
	if fail != codes.OK {
		return nil, status.Error(fail, "pinned backend failure")
	}
	return &trillian.GetInclusionProofByHashResponse{
		Proof: []*trillian.Proof{{LeafIndex: pinnedLeafIndex, Hashes: pinnedProofHashes()}},
	}, nil
}

func (s *stubLog) GetConsistencyProof(ctx context.Context, req *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	return &trillian.GetConsistencyProofResponse{
		Proof: &trillian.Proof{LeafIndex: 0, Hashes: pinnedProofHashes()},
	}, nil
}

func (s *stubLog) GetEntryAndProof(ctx context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	return &trillian.GetEntryAndProofResponse{
		Proof: &trillian.Proof{LeafIndex: req.LeafIndex, Hashes: pinnedProofHashes()},
		Leaf: &trillian.LogLeaf{
			LeafIndex: req.LeafIndex,
			LeafValue: pinnedStoredLeaf(),
			ExtraData: pinnedStoredExtraData(),
		},
	}, nil
}

// The remaining methods of this pin's TrillianLogServer are not on any CTFE
// code path at v1.0; the interface has no Unimplemented embed, so they are
// declared explicitly and refuse.
func (s *stubLog) QueueLeaf(context.Context, *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	return nil, status.Error(codes.Unimplemented, "QueueLeaf")
}
func (s *stubLog) GetInclusionProof(context.Context, *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	return nil, status.Error(codes.Unimplemented, "GetInclusionProof")
}
func (s *stubLog) GetSequencedLeafCount(context.Context, *trillian.GetSequencedLeafCountRequest) (*trillian.GetSequencedLeafCountResponse, error) {
	return nil, status.Error(codes.Unimplemented, "GetSequencedLeafCount")
}
func (s *stubLog) GetLeavesByHash(context.Context, *trillian.GetLeavesByHashRequest) (*trillian.GetLeavesByHashResponse, error) {
	return nil, status.Error(codes.Unimplemented, "GetLeavesByHash")
}
