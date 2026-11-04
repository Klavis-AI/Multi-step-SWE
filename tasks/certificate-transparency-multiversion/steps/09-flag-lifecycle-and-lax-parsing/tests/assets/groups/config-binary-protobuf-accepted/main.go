// Group config-binary-protobuf-accepted.
//
// At v1.1.2 both configuration entry points are documented to accept a file
// that "should contain text or binary-encoded protobuf configuration data".
// v1.1.0 accepted text only.
//
// The driver encodes the protobuf wire format itself from the published field
// numbers in config.proto, so nothing about the acceptance path is assumed:
// the bytes on disk are a plain binary-encoded LogConfigSet / LogMultiConfig
// and the candidate either parses them or it does not.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"

	"ctverify/vhelp"
)

// ---- minimal protobuf wire encoder (proto3, no reflection) ----------------

func uvarint(v uint64) []byte {
	var out []byte
	for v >= 0x80 {
		out = append(out, byte(v)|0x80)
		v >>= 7
	}
	return append(out, byte(v))
}

func tag(field, wire int) []byte { return uvarint(uint64(field)<<3 | uint64(wire)) }

// bytesField encodes a length-delimited field (string, bytes or message).
func bytesField(field int, body []byte) []byte {
	out := tag(field, 2)
	out = append(out, uvarint(uint64(len(body)))...)
	return append(out, body...)
}

func strField(field int, s string) []byte { return bytesField(field, []byte(s)) }

func varField(field int, v uint64) []byte { return append(tag(field, 0), uvarint(v)...) }

func cat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}

// ---- configpb field numbers, from config.proto ---------------------------
//
//	LogBackend     { name = 1, backend_spec = 2 }
//	LogBackendSet  { backend = 1 (repeated) }
//	LogConfigSet   { config = 1 (repeated) }
//	LogConfig      { log_id = 1, prefix = 2, roots_pem_file = 3, public_key = 5,
//	                 ext_key_usages = 7, log_backend_name = 11, is_mirror = 12 }
//	LogMultiConfig { backends = 1, log_configs = 2 }
//	keyspb.PublicKey { der = 1 }

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
	}
	panic("bad hex")
}

func publicKeyMsg() []byte { return bytesField(1, unhex(spkiHex)) }

// mirrorLogConfig is a complete, individually valid LogConfig.
func mirrorLogConfig(id uint64, prefix, backend string, roots, ekus []string) []byte {
	parts := [][]byte{
		varField(1, id),
		strField(2, prefix),
	}
	for _, r := range roots {
		parts = append(parts, strField(3, r))
	}
	parts = append(parts, bytesField(5, publicKeyMsg()))
	for _, e := range ekus {
		parts = append(parts, strField(7, e))
	}
	parts = append(parts, strField(11, backend))
	parts = append(parts, varField(12, 1)) // is_mirror
	return cat(parts...)
}

func write(t *vhelp.T, name string, body []byte) string {
	p := filepath.Join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, body, 0600); err != nil {
		t.Fatalf("write %s: %v", name, err)
	}
	return p
}

func main() {
	t := vhelp.New()

	alpha := mirrorLogConfig(1001, "binary-alpha", "be-a",
		[]string{"/pinned/a-roots.pem", "/pinned/b-roots.pem"}, []string{"ClientAuth"})
	beta := mirrorLogConfig(1002, "binary-beta", "be-a", nil, nil)

	setBytes := cat(bytesField(1, alpha), bytesField(1, beta))

	// 1. A binary-encoded LogConfigSet loads, in order.
	binSet := write(t, "logconfigset.bin", setBytes)
	cfgs, err := ctfe.LogConfigFromFile(binSet)
	if err != nil {
		t.Check("binary-logconfigset-accepted", false, "LogConfigFromFile: %v", err)
		t.Check("binary-logconfig-fields-exact", false, "no config to inspect")
	} else {
		got := ""
		for _, c := range cfgs {
			got += fmt.Sprintf("%d/%s ", c.LogId, c.Prefix)
		}
		t.Equal("binary-logconfigset-accepted", got, "1001/binary-alpha 1002/binary-beta ")

		// 2. Every field survives the binary decode with its exact value.
		if len(cfgs) == 0 {
			t.Check("binary-logconfig-fields-exact", false, "empty set")
		} else {
			c := cfgs[0]
			t.Equal("binary-logconfig-fields-exact",
				fmt.Sprintf("%q|%q|%s|%v|%d", c.RootsPemFile, c.ExtKeyUsages,
					c.LogBackendName, c.IsMirror, len(c.PublicKey.GetDer())),
				`["/pinned/a-roots.pem" "/pinned/b-roots.pem"]|["ClientAuth"]|be-a|true|91`)
		}
	}

	// 3. A binary-encoded LogMultiConfig loads.
	backendSet := bytesField(1, cat(strField(1, "be-a"), strField(2, "127.0.0.1:65000")))
	multiBytes := cat(bytesField(1, backendSet), bytesField(2, setBytes))
	binMulti := write(t, "logmulticonfig.bin", multiBytes)
	mc, err := ctfe.MultiLogConfigFromFile(binMulti)
	if err != nil {
		t.Check("binary-logmulticonfig-accepted", false, "MultiLogConfigFromFile: %v", err)
		t.Check("binary-logmulticonfig-validates", false, "no config to validate")
	} else {
		b := mc.Backends.GetBackend()
		c := mc.LogConfigs.GetConfig()
		ok := len(b) == 1 && len(c) == 2
		desc := "unexpected shape"
		if ok {
			desc = fmt.Sprintf("%s@%s+%d", b[0].Name, b[0].BackendSpec, len(c))
		}
		t.Equal("binary-logmulticonfig-accepted", desc, "be-a@127.0.0.1:65000+2")

		// 4. And the decoded message is a config the validator accepts,
		//    resolving to exactly one named backend.
		bm, verr := ctfe.ValidateLogMultiConfig(mc)
		if verr != nil {
			t.Check("binary-logmulticonfig-validates", false, "ValidateLogMultiConfig: %v", verr)
		} else {
			names := make([]string, 0, len(bm))
			for k := range bm {
				names = append(names, k)
			}
			sort.Strings(names)
			t.Equal("binary-logmulticonfig-validates",
				fmt.Sprintf("%v", names), "[be-a]")
		}
	}

	// 5. Text protobuf is still accepted: the binary path is additional, not a
	//    replacement.
	txt := write(t, "logconfigset.textproto", []byte(
		"config {\n  log_id: 7\n  prefix: \"text-alpha\"\n  is_mirror: true\n}\n"))
	tcfgs, terr := ctfe.LogConfigFromFile(txt)
	if terr != nil {
		t.Check("text-logconfigset-still-accepted", false, "LogConfigFromFile: %v", terr)
	} else {
		t.Equal("text-logconfigset-still-accepted",
			fmt.Sprintf("%d/%s/%d", len(tcfgs), tcfgs[0].Prefix, tcfgs[0].LogId),
			"1/text-alpha/7")
	}

	// 6. A file that is neither encoding is rejected by both entry points.
	//    0xff x4 is a truncated varint tag in binary and invalid UTF-8 in text.
	junk := write(t, "junk.bin", []byte{0xff, 0xff, 0xff, 0xff})
	_, e1 := ctfe.LogConfigFromFile(junk)
	_, e2 := ctfe.MultiLogConfigFromFile(junk)
	t.Check("unparseable-file-rejected", e1 != nil && e2 != nil,
		"LogConfigFromFile err=%v MultiLogConfigFromFile err=%v", e1, e2)

	// 7. And the refusal identifies what it could not build and what it read:
	//    the message type (`LogConfigSet` / `LogMultiConfig`, CONFIG.md §1 and
	//    the §4.2 format) and the path of the file. Those two identifiers are
	//    fixed by the documents; the sentence they sit in is not, and
	//    TEST_INTERFACE.md §3.3 forbids grading it, so nothing else about the
	//    message -- its order, its wording, or the protobuf library's own
	//    reasons -- is compared.
	bad := namesInputs("LogConfigSet", junk, e1) + namesInputs("LogMultiConfig", junk, e2)
	t.Check("unparseable-file-names-type-and-path", bad == "",
		"the refusal must name the message type and the file:%s", bad)

	t.Done()
}

// namesInputs returns "" if err identifies both the message type `what` and
// the file it was read from, or a description of what is missing. Only those
// two identifiers are looked for: everything else in the message belongs to
// the candidate or to the protobuf library.
func namesInputs(what, filename string, err error) string {
	if err == nil {
		return fmt.Sprintf(" %s: no error at all", what)
	}
	msg := err.Error()
	missing := ""
	if !strings.Contains(msg, what) {
		missing += " message type"
	}
	if !strings.Contains(msg, filename) {
		missing += " file name"
	}
	if missing == "" {
		return ""
	}
	return fmt.Sprintf(" %s: %q names no%s", what, clip(msg), missing)
}

func clip(s string) string {
	if len(s) > 300 {
		return s[:300] + "..."
	}
	return s
}
