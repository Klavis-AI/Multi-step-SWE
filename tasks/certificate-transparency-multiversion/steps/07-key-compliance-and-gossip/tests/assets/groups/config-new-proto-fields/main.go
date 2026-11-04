// Group config-new-proto-fields.
//
// v1.0.21 adds four fields to configpb.LogConfig (CONFIG.md sections 2.1, 2.2
// and 2.5): is_mirror (12), override_handler_prefix (13),
// max_merge_delay_sec (14) and expected_merge_delay_sec (15). A field number
// is part of the format, not an implementation detail, so each is observed on
// the wire as well as through the generated accessors and through the text
// proto the server actually reads.
//
// The expected wire bytes are built here from the field number and the value
// with encoding/binary's varint encoder, independently of the protobuf
// runtime, so the comparison is against the protobuf spec rather than against
// the library under test.
//
// v1.0.19's LogConfig has eleven fields and none of these four, so the driver
// does not compile against that tree.
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"

	"github.com/golang/protobuf/proto"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

const (
	pinnedLogID    = int64(1001)
	pinnedPrefix   = "bench"
	pinnedOverride = "/otherlogs"
	pinnedMMD      = int32(86400)
	pinnedEMD      = int32(120)
)

// tagVarint renders the protobuf key byte for <field, wire type 0> followed by
// the value as a varint. Field numbers below 16 always fit in one key byte.
func tagVarint(field int, value uint64) []byte {
	buf := make([]byte, binary.MaxVarintLen64)
	n := binary.PutUvarint(buf, value)
	return append([]byte{byte(field<<3 | 0)}, buf[:n]...)
}

// tagBytes renders the key byte for <field, wire type 2> plus a length-delimited payload.
func tagBytes(field int, payload string) []byte {
	buf := make([]byte, binary.MaxVarintLen64)
	n := binary.PutUvarint(buf, uint64(len(payload)))
	out := append([]byte{byte(field<<3 | 2)}, buf[:n]...)
	return append(out, []byte(payload)...)
}

func marshal(t *vhelp.T, id string, m proto.Message) []byte {
	b, err := proto.Marshal(m)
	if err != nil {
		t.Fatalf("%s: proto.Marshal: %v", id, err)
	}
	return b
}

func main() {
	t := vhelp.New()

	full := &configpb.LogConfig{
		LogId:                 pinnedLogID,
		Prefix:                pinnedPrefix,
		OverrideHandlerPrefix: pinnedOverride,
		IsMirror:              true,
		MaxMergeDelaySec:      pinnedMMD,
		ExpectedMergeDelaySec: pinnedEMD,
	}

	// --- the generated accessors --------------------------------------------
	t.Equal("getter-override-handler-prefix", full.GetOverrideHandlerPrefix(), pinnedOverride)
	t.Equal("getter-is-mirror", full.GetIsMirror(), true)
	t.Equal("getter-max-merge-delay-sec", full.GetMaxMergeDelaySec(), pinnedMMD)
	t.Equal("getter-expected-merge-delay-sec", full.GetExpectedMergeDelaySec(), pinnedEMD)

	// A nil *LogConfig answers the zero value rather than panicking, which is
	// what makes these safe to call on an absent config.
	var absent *configpb.LogConfig
	ok := absent.GetOverrideHandlerPrefix() == "" && !absent.GetIsMirror() &&
		absent.GetMaxMergeDelaySec() == 0 && absent.GetExpectedMergeDelaySec() == 0
	t.Check("getters-on-nil-config-are-zero", ok,
		"nil config answered %q/%v/%d/%d", absent.GetOverrideHandlerPrefix(),
		absent.GetIsMirror(), absent.GetMaxMergeDelaySec(), absent.GetExpectedMergeDelaySec())

	// --- the four field numbers, on the wire ---------------------------------
	t.EqualBytes("wire-field-12-is-mirror",
		marshal(t, "is_mirror", &configpb.LogConfig{IsMirror: true}),
		tagVarint(12, 1))

	t.EqualBytes("wire-field-13-override-handler-prefix",
		marshal(t, "override_handler_prefix", &configpb.LogConfig{OverrideHandlerPrefix: pinnedOverride}),
		tagBytes(13, pinnedOverride))

	t.EqualBytes("wire-field-14-max-merge-delay-sec",
		marshal(t, "max_merge_delay_sec", &configpb.LogConfig{MaxMergeDelaySec: pinnedMMD}),
		tagVarint(14, uint64(pinnedMMD)))

	t.EqualBytes("wire-field-15-expected-merge-delay-sec",
		marshal(t, "expected_merge_delay_sec", &configpb.LogConfig{ExpectedMergeDelaySec: pinnedEMD}),
		tagVarint(15, uint64(pinnedEMD)))

	// --- the names the server's own config file uses --------------------------
	scratch := os.Getenv("CT_SCRATCH")
	good := filepath.Join(scratch, "new-fields.textproto")
	body := fmt.Sprintf(`config {
  log_id: %d
  prefix: %q
  override_handler_prefix: %q
  is_mirror: true
  max_merge_delay_sec: %d
  expected_merge_delay_sec: %d
}
`, pinnedLogID, pinnedPrefix, pinnedOverride, pinnedMMD, pinnedEMD)
	if err := os.WriteFile(good, []byte(body), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	cfgs, err := ctfe.LogConfigFromFile(good)
	switch {
	case err != nil:
		t.Check("textproto-round-trip", false, "LogConfigFromFile refused the four new fields: %v", err)
	case len(cfgs) != 1:
		t.Check("textproto-round-trip", false, "want 1 config, got %d", len(cfgs))
	default:
		got := cfgs[0]
		same := got.GetOverrideHandlerPrefix() == pinnedOverride && got.GetIsMirror() &&
			got.GetMaxMergeDelaySec() == pinnedMMD && got.GetExpectedMergeDelaySec() == pinnedEMD
		t.Check("textproto-round-trip", same,
			"read back %q/%v/%d/%d", got.GetOverrideHandlerPrefix(), got.GetIsMirror(),
			got.GetMaxMergeDelaySec(), got.GetExpectedMergeDelaySec())
		// Re-marshalling the config read from the file must give the same bytes
		// as marshalling the literal above: the file and the struct agree.
		t.EqualBytes("textproto-and-struct-agree",
			marshal(t, "read-back", got), marshal(t, "literal", full))
	}

	// Control: the text-proto reader is strict, so the four names above being
	// accepted is evidence that the fields exist and not that unknown names
	// are ignored.
	bad := filepath.Join(scratch, "unknown-field.textproto")
	if err := os.WriteFile(bad, []byte("config {\n  log_id: 1001\n  prefix: \"bench\"\n  no_such_config_field: 7\n}\n"), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	_, err = ctfe.LogConfigFromFile(bad)
	t.Check("textproto-unknown-field-rejected", err != nil,
		"a config naming a field no release has was accepted")

	// Control: an empty set is refused, so "it parsed" is never vacuous.
	empty := filepath.Join(scratch, "empty.textproto")
	if err := os.WriteFile(empty, []byte(""), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	_, err = ctfe.LogConfigFromFile(empty)
	t.Check("textproto-empty-set-rejected", err != nil, "an empty config file was accepted")

	_ = bytes.Equal
	t.Done()
}
