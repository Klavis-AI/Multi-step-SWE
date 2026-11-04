// Group config-validation-new-fields.
//
// v1.1.0 adds three fields to configpb.LogConfig -- reject_unexpired (17),
// frozen_sth (16) and reject_extensions (18) -- and teaches
// ctfe.ValidateLogConfig about the first two. None of the three exists at
// v1.0.21, and neither does the configpb.SignedTreeHead message, so a v1.0.21
// tree cannot even compile this driver.
//
// Everything here is pinned: the log key pair is the minted corpus's, and the
// frozen STH is a fixed (tree_size, timestamp, root hash) triple carrying a
// signature computed once and embedded below, so no clock and no randomness
// enter an assertion.
package main

import (
	"encoding/hex"

	"ctverify/vhelp"

	"github.com/golang/protobuf/ptypes"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"
)

const (
	// SubjectPublicKeyInfo of minted/keys/log-key.pem.
	pubDERHex = "3059301306072a8648ce3d020106082a8648ce3d0301070342000457609453c6" +
		"d4b60d3d0826690f2b80af73b8e2924f3a54df8c8b06511e5372c54e21bfcb2d77ff29b3" +
		"05a6fa4f9788138c146a969c0f3d74cb09659ad791fac9"
	// SHA-256("harbor-ct-bench frozen sth root").
	rootHashHex = "0617c7c846ca8a3b3f75148b399d0968b1e6201f487667ab53a06e7aeb8bc967"
	// tls-serialised DigitallySigned over the RFC 6962 STH signature input for
	// tree_size=4096, timestamp=1573689600000, the root hash above, made with
	// minted/keys/log-key.pem.
	sthSigHex = "0403004630440220321b76d7186c379feba36102096cfa120c0af8f538099aa554" +
		"100567d080f2450220390e4dbed6567372b4571b68b1d7531e8f12892e4ba9da2c891abd" +
		"6b7d0e9646"

	frozenTreeSize  = int64(4096)
	frozenTimestamp = int64(1573689600000)
)

func unhex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad pinned hex constant: %v", err)
	}
	return b
}

func main() {
	t := vhelp.New()

	pubDER := unhex(t, pubDERHex)
	rootHash := unhex(t, rootHashHex)
	sthSig := unhex(t, sthSigHex)

	privAny, err := ptypes.MarshalAny(&keyspb.PEMKeyFile{Path: vhelp.Fixture("minted", "keys", "log-key.pem")})
	if err != nil {
		t.Fatalf("could not wrap the pinned private key reference: %v", err)
	}

	// base is a configuration that validates, so every rejection below is
	// attributable to the one field the case changes.
	base := func() *configpb.LogConfig {
		return &configpb.LogConfig{
			LogId:                 1001,
			Prefix:                "bench",
			RootsPemFile:          []string{vhelp.Fixture("minted", "roots-bench-plus-int.pem")},
			PrivateKey:            privAny,
			PublicKey:             &keyspb.PublicKey{Der: pubDER},
			MaxMergeDelaySec:      86400,
			ExpectedMergeDelaySec: 120,
		}
	}
	frozen := func() *configpb.SignedTreeHead {
		return &configpb.SignedTreeHead{
			TreeSize:          frozenTreeSize,
			Timestamp:         frozenTimestamp,
			Sha256RootHash:    append([]byte(nil), rootHash...),
			TreeHeadSignature: append([]byte(nil), sthSig...),
		}
	}

	accepts := func(id string, cfg *configpb.LogConfig) *ctfe.ValidatedLogConfig {
		v, err := ctfe.ValidateLogConfig(cfg)
		t.Check(id, err == nil && v != nil, "ValidateLogConfig returned err=%v", err)
		return v
	}
	rejects := func(id string, cfg *configpb.LogConfig) {
		v, err := ctfe.ValidateLogConfig(cfg)
		got := "<nil>"
		if err != nil {
			got = err.Error()
		}
		t.Check(id, err != nil && v == nil,
			"ValidateLogConfig accepted the config (err=%q, validated=%v); want a rejection", got, v != nil)
	}

	// --- the baseline, so no rejection below is vacuous ----------------------
	accepts("baseline-config-accepted", base())

	// --- reject_unexpired ----------------------------------------------------
	c := base()
	c.RejectExpired = true
	accepts("reject-expired-alone-accepted", c)

	c = base()
	c.RejectUnexpired = true
	accepts("reject-unexpired-alone-accepted", c)

	// The combination admits nothing at all, so v1.1.0 refuses to start on it.
	c = base()
	c.RejectExpired, c.RejectUnexpired = true, true
	rejects("reject-expired-and-unexpired-rejected", c)

	// --- frozen_sth ----------------------------------------------------------
	c = base()
	c.FrozenSth = frozen()
	v := accepts("frozen-sth-accepted", c)
	if v != nil && v.FrozenSTH != nil {
		t.Equal("frozen-sth-tree-size", v.FrozenSTH.TreeSize, uint64(frozenTreeSize))
		t.Equal("frozen-sth-timestamp", v.FrozenSTH.Timestamp, uint64(frozenTimestamp))
		t.EqualBytes("frozen-sth-root-hash", v.FrozenSTH.SHA256RootHash[:], rootHash)
	} else {
		t.Check("frozen-sth-tree-size", false, "no validated frozen STH")
		t.Check("frozen-sth-timestamp", false, "no validated frozen STH")
		t.Check("frozen-sth-root-hash", false, "no validated frozen STH")
	}

	// A frozen STH is meaningless without the key that signed it.
	c = base()
	c.FrozenSth = frozen()
	c.PublicKey = nil
	rejects("frozen-sth-without-public-key-rejected", c)

	// The signature is checked, not merely parsed: one changed field breaks it.
	c = base()
	c.FrozenSth = frozen()
	c.FrozenSth.TreeSize = frozenTreeSize + 1
	rejects("frozen-sth-tampered-tree-size-rejected", c)

	c = base()
	c.FrozenSth = frozen()
	c.FrozenSth.Timestamp = frozenTimestamp + 1
	rejects("frozen-sth-tampered-timestamp-rejected", c)

	// A 31-byte root hash is not an STH.
	c = base()
	c.FrozenSth = frozen()
	c.FrozenSth.Sha256RootHash = c.FrozenSth.Sha256RootHash[:31]
	rejects("frozen-sth-short-root-hash-rejected", c)

	// So is a blob that is not a DigitallySigned.
	c = base()
	c.FrozenSth = frozen()
	c.FrozenSth.TreeHeadSignature = []byte{0x04, 0x03}
	rejects("frozen-sth-unparsable-signature-rejected", c)

	t.Done()
}
