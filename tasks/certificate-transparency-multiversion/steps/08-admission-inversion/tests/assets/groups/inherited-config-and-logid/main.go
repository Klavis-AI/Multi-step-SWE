// Group inherited-config-and-logid.
//
// Step 2's boundary, in the part that needs no ctfe import: the RFC 6962 log
// id, and the configpb messages and text-protobuf grammar that the log
// configuration is read through.
//
// ctfe.ValidateLogMultiConfig cannot be reached from a v1.0.21 tree -- its
// own package fails to build there, against this step's manifest -- so it is
// left out of this group entirely. It is scored in its own right at steps 2,
// 3 and 9. What is left here is genuinely inherited: logid/ is byte-identical at v1.0.21 and
// v1.1.0, and so is every field of configpb.LogConfig this group touches.
//
// The group is deliberately anchored on what a do-nothing continuation is
// entitled to: the four logid constructors and their exact error behaviour,
// and the text form of a configuration as it is parsed back off disk, with
// every field pinned.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"io/ioutil"
	"os"
	"path/filepath"

	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes"
	"github.com/google/certificate-transparency-go/logid"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"

	"ctverify/vhelp"
)

const (
	// SubjectPublicKeyInfo of minted/keys/log-key.pem, and the RFC 6962 LogID
	// that RFC 6962 section 3.2 derives from it.
	pubDERHex = "3059301306072a8648ce3d020106082a8648ce3d0301070342000457609453c6" +
		"d4b60d3d0826690f2b80af73b8e2924f3a54df8c8b06511e5372c54e21bfcb2d77ff29b3" +
		"05a6fa4f9788138c146a969c0f3d74cb09659ad791fac9"
	wantLogIDB64 = "NOotw9BtP5lIZGvg2xA8czI9h4fHIq0QPXPgsJVJSqU="

	// A text configuration in the grammar ctfe.LogConfigFromFile accepts. It
	// is written to a scratch file and read back, so the group grades the
	// on-disk path a real deployment uses, not just an in-memory struct.
	cfgText = `
config {
  log_id: 1001
  prefix: "bench"
  roots_pem_file: "roots-bench-plus-int.pem"
  roots_pem_file: "bench-root.pem"
  private_key: {
    [type.googleapis.com/keyspb.PEMKeyFile]: { path: "log-key.pem" }
  }
  public_key: { der: "\x30\x59\x30\x13\x06\x07\x2a\x86\x48\xce\x3d\x02\x01\x06\x08\x2a\x86\x48\xce\x3d\x03\x01\x07\x03\x42\x00\x04" }
  max_merge_delay_sec: 86400
  expected_merge_delay_sec: 120
}
`
)

func main() {
	t := vhelp.New()

	pubDER, err := hex.DecodeString(pubDERHex)
	if err != nil {
		t.Fatalf("bad pinned hex constant: %v", err)
	}

	// --- logid ---------------------------------------------------------------
	id, err := logid.FromPubKeyB64(base64.StdEncoding.EncodeToString(pubDER))
	t.Check("logid-from-pubkey-b64-exact",
		err == nil && base64.StdEncoding.EncodeToString(id[:]) == wantLogIDB64,
		"err=%v id=%s", err, base64.StdEncoding.EncodeToString(id[:]))

	back, err := logid.FromB64(wantLogIDB64)
	t.Check("logid-from-b64-roundtrip",
		err == nil && back == id && back.String() == "logid:["+wantLogIDB64+"]",
		"err=%v roundtrip=%v string=%q", err, back == id, back.String())

	l, err2 := logid.FromBytes(id[:])
	t.Check("logid-frombytes-roundtrip",
		err2 == nil && l == id,
		"err=%v roundtrip=%v", err2, l == id)

	_, e31 := logid.FromBytes(make([]byte, 31))
	_, e33 := logid.FromBytes(make([]byte, 33))
	t.Check("logid-frombytes-rejects-wrong-length", e31 != nil && e33 != nil,
		"31 bytes: %v; 33 bytes: %v", e31, e33)

	// The log id is the SHA-256 of the SubjectPublicKeyInfo, so it must agree
	// with a hash taken here rather than only with itself.
	sum := sha256.Sum256(pubDER)
	t.Check("logid-is-sha256-of-spki", string(sum[:]) == string(id[:]),
		"sha256(spki)=%s logid=%s", base64.StdEncoding.EncodeToString(sum[:]),
		base64.StdEncoding.EncodeToString(id[:]))

	// --- the text configuration grammar --------------------------------------
	dir, err := ioutil.TempDir(os.Getenv("CT_SCRATCH"), "cfg")
	if err != nil {
		t.Fatalf("scratch dir: %v", err)
	}
	path := filepath.Join(dir, "bench.cfg")
	if err := ioutil.WriteFile(path, []byte(cfgText), 0644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
	raw, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatalf("read back %s: %v", path, err)
	}

	var set configpb.LogConfigSet
	if err := proto.UnmarshalText(string(raw), &set); err != nil {
		t.Check("text-config-parses", false, "err=%v", err)
		t.Done()
		return
	}
	t.Check("text-config-parses", len(set.Config) == 1, "got %d entries, want 1", len(set.Config))
	if len(set.Config) != 1 {
		t.Done()
		return
	}
	c := set.Config[0]

	t.Check("text-config-scalar-fields", c.LogId == 1001 && c.Prefix == "bench" &&
		c.MaxMergeDelaySec == 86400 && c.ExpectedMergeDelaySec == 120,
		"log_id=%d prefix=%q max=%d expected=%d", c.LogId, c.Prefix,
		c.MaxMergeDelaySec, c.ExpectedMergeDelaySec)

	// A repeated string field keeps its order.
	t.Check("text-config-repeated-roots-file-order",
		len(c.RootsPemFile) == 2 && c.RootsPemFile[0] == "roots-bench-plus-int.pem" &&
			c.RootsPemFile[1] == "bench-root.pem",
		"roots_pem_file=%v", c.RootsPemFile)

	// The bytes form of public_key survives the escape sequence in the text.
	t.Check("text-config-public-key-bytes",
		len(c.PublicKey.GetDer()) > 0 && hex.EncodeToString(c.PublicKey.GetDer()[:5]) == "3059301306",
		"der=%s", hex.EncodeToString(c.PublicKey.GetDer()))

	// private_key is a google.protobuf.Any whose type URL names the key source
	// the deployment will resolve at start-up; the reference itself is what is
	// pinned, not any material.
	var pk keyspb.PEMKeyFile
	privErr := ptypes.UnmarshalAny(c.PrivateKey, &pk)
	t.Check("text-config-private-key-is-pem-key-file",
		privErr == nil && c.PrivateKey.GetTypeUrl() == "type.googleapis.com/keyspb.PEMKeyFile" &&
			pk.Path == "log-key.pem",
		"type_url=%q path=%q err=%v", c.PrivateKey.GetTypeUrl(), pk.Path, privErr)

	// Fields the text form leaves out are the zero value, not some default
	// the parser invented: log_backend_name is set by ToMultiLogConfig.
	t.Check("text-config-unset-field-is-zero",
		c.LogBackendName == "" && c.RejectExpired == false,
		"log_backend_name=%q reject_expired=%v", c.LogBackendName, c.RejectExpired)

	// A second entry is an independent message, not a shared pointer.
	second := &configpb.LogConfigSet{}
	if err := proto.UnmarshalText("config { log_id: 7 prefix: \"other\" }\n", second); err != nil {
		t.Check("text-config-second-entry-independent", false, "err=%v", err)
	} else {
		t.Check("text-config-second-entry-independent",
			len(second.Config) == 1 && second.Config[0].LogId == 7 &&
				second.Config[0].Prefix == "other" && c.LogId == 1001,
			"second=%d/%q first=%d/%q", second.Config[0].LogId, second.Config[0].Prefix, c.LogId, c.Prefix)
	}

	// --- the message itself --------------------------------------------------
	// LogMultiConfig is the shape the whole step-2 boundary is expressed in, so
	// its oneof-free nesting is pinned here without going through ctfe.
	mc := &configpb.LogMultiConfig{
		LogConfigs: &configpb.LogConfigSet{Config: []*configpb.LogConfig{c}},
		Backends:   &configpb.LogBackendSet{Backend: []*configpb.LogBackend{{Name: "default", BackendSpec: "localhost:8090"}}},
	}
	round, err := proto.Marshal(mc)
	if err != nil {
		t.Check("multiconfig-proto-roundtrip", false, "marshal: %v", err)
	} else {
		var back configpb.LogMultiConfig
		err = proto.Unmarshal(round, &back)
		t.Check("multiconfig-proto-roundtrip",
			err == nil && len(back.LogConfigs.GetConfig()) == 1 && back.LogConfigs.GetConfig()[0].LogId == 1001 &&
				len(back.Backends.GetBackend()) == 1 && back.Backends.GetBackend()[0].Name == "default" &&
				back.Backends.GetBackend()[0].BackendSpec == "localhost:8090",
			"err=%v", err)
	}

	t.Done()
}
