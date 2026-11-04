// Group config-validate-logconfigs.
//
// v1.1.2 factors the single-server configuration rules out of
// ValidateLogMultiConfig into a new exported entry point,
// ctfe.ValidateLogConfigs, whose contract is stated in its own documentation:
// every log config must be individually valid, prefixes must be distinct and
// non-empty, and tree IDs must be distinct across the whole set.
//
// Observed per function on the exported boundary: accept or reject, nothing
// else. Every config is a mirror, so nothing is read from disk.
package main

import (
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"

	"ctverify/vhelp"
)

const spkiHex = "3059301306072a8648ce3d020106082a8648ce3d03010703420004" +
	"57609453c6d4b60d3d0826690f2b80af73b8e2924f3a54df8c8b06511e5372c5" +
	"4e21bfcb2d77ff29b305a6fa4f9788138c146a969c0f3d74cb09659ad791fac9"

func unhex(s string) []byte {
	out := make([]byte, 0, len(s)/2)
	for i := 0; i+1 < len(s); i += 2 {
		out = append(out, byte(hexVal(s[i])<<4|hexVal(s[i+1])))
	}
	return out
}

func hexVal(c byte) int {
	switch {
	case c >= '0' && c <= '9':
		return int(c - '0')
	case c >= 'a' && c <= 'f':
		return int(c-'a') + 10
	case c >= 'A' && c <= 'F':
		return int(c-'A') + 10
	}
	panic("bad hex")
}

func mirror(id int64, prefix string) *configpb.LogConfig {
	return &configpb.LogConfig{
		LogId:     id,
		Prefix:    prefix,
		IsMirror:  true,
		PublicKey: &keyspb.PublicKey{Der: unhex(spkiHex)},
	}
}

func main() {
	t := vhelp.New()

	accept := func(id string, cfgs ...*configpb.LogConfig) {
		err := ctfe.ValidateLogConfigs(cfgs)
		t.Check(id, err == nil, "ValidateLogConfigs rejected a valid set: %v", err)
	}
	reject := func(id string, cfgs ...*configpb.LogConfig) {
		err := ctfe.ValidateLogConfigs(cfgs)
		t.Check(id, err != nil, "ValidateLogConfigs accepted a set it must reject")
	}

	// 1. One valid config.
	accept("single-config-accepted", mirror(1, "alpha"))

	// 2. Distinct prefixes and distinct tree IDs.
	accept("distinct-configs-accepted", mirror(1, "alpha"), mirror(2, "beta"))

	// 3. Rule 2: prefixes must be distinct.
	reject("duplicate-prefix-rejected", mirror(1, "alpha"), mirror(2, "alpha"))

	// 4. Rule 2: a prefix must not be empty.
	reject("empty-prefix-rejected", mirror(1, "alpha"), mirror(2, ""))

	// 5. Rule 3: tree IDs must be distinct across the whole set, which is
	// stricter than the per-backend rule the multi-backend validator applies.
	reject("duplicate-tree-id-rejected", mirror(7, "alpha"), mirror(7, "beta"))

	// 6. Rule 1: an individually invalid config fails the set. A zero log ID is
	// rejected by ValidateLogConfig.
	reject("invalid-member-rejected", mirror(1, "alpha"), mirror(0, "beta"))

	t.Done()
}
