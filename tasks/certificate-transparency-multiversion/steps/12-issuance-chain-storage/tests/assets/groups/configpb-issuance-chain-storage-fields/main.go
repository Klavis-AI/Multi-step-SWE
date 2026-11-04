// Observes the two LogConfig fields this release adds to the CTFE
// configuration schema, at the schema boundary rather than the validation
// boundary: their field numbers on the wire, their text-proto spellings, and
// the name and members of the new IssuanceChainStorageBackend enum. A
// configuration file written against this release must be byte-compatible
// with the schema, so these are exact-encoding assertions.
package main

import (
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// Field 20, wire type 2: tag varint 20<<3|2 = 162 -> 0xa2 0x01.
	b, err := proto.Marshal(&configpb.LogConfig{CtfeStorageConnectionString: "ab"})
	if err != nil {
		t.Fatalf("marshal connection string: %v", err)
	}
	t.EqualBytes("ctfe-storage-connection-string-is-field-20", b,
		[]byte{0xa2, 0x01, 0x02, 'a', 'b'})

	// Field 21, wire type 0: tag varint 21<<3|0 = 168 -> 0xa8 0x01, value 1.
	b, err = proto.Marshal(&configpb.LogConfig{
		ExtraDataIssuanceChainStorageBackend: configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE,
	})
	if err != nil {
		t.Fatalf("marshal backend: %v", err)
	}
	t.EqualBytes("extra-data-backend-is-field-21", b, []byte{0xa8, 0x01, 0x01})

	// The zero enum member is the default, so it must not appear on the wire.
	b, err = proto.Marshal(&configpb.LogConfig{
		ExtraDataIssuanceChainStorageBackend: configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC,
	})
	if err != nil {
		t.Fatalf("marshal default backend: %v", err)
	}
	t.EqualBytes("trillian-grpc-is-the-zero-enum-member", b, []byte{})

	t.Check("enum-member-numbers",
		configpb.LogConfig_IssuanceChainStorageBackend_value["ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC"] == 0 &&
			configpb.LogConfig_IssuanceChainStorageBackend_value["ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE"] == 1 &&
			len(configpb.LogConfig_IssuanceChainStorageBackend_value) == 2,
		"enum value map is %v, want exactly TRILLIAN_GRPC=0 and CTFE=1",
		configpb.LogConfig_IssuanceChainStorageBackend_value)

	// The text-proto spellings are what an operator writes in the config file.
	const text = `log_id: 1001
prefix: "bench"
ctfe_storage_connection_string: "mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe"
extra_data_issuance_chain_storage_backend: ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE
`
	var cfg configpb.LogConfig
	if err := prototext.Unmarshal([]byte(text), &cfg); err != nil {
		t.Fatalf("prototext.Unmarshal: %v", err)
	}
	t.Check("textproto-field-names",
		cfg.GetCtfeStorageConnectionString() == "mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe" &&
			cfg.GetExtraDataIssuanceChainStorageBackend() == configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE,
		"parsed text proto gave (%q, %v)", cfg.GetCtfeStorageConnectionString(), cfg.GetExtraDataIssuanceChainStorageBackend())

	var bad configpb.LogConfig
	err = prototext.Unmarshal([]byte("extra_data_issuance_chain_storage_backend: ISSUANCE_CHAIN_STORAGE_BACKEND_POSTGRES\n"), &bad)
	t.Check("unknown-enum-member-rejected", err != nil,
		"an unknown enum member parsed without error")

	// The descriptor names, which is what a proto consumer in another language
	// binds to.
	fields := (&configpb.LogConfig{}).ProtoReflect().Descriptor().Fields()
	f20 := fields.ByNumber(protoreflect.FieldNumber(20))
	f21 := fields.ByNumber(protoreflect.FieldNumber(21))
	ok := f20 != nil && f21 != nil &&
		string(f20.Name()) == "ctfe_storage_connection_string" &&
		f20.Kind() == protoreflect.StringKind &&
		string(f21.Name()) == "extra_data_issuance_chain_storage_backend" &&
		f21.Kind() == protoreflect.EnumKind &&
		string(f21.Enum().FullName()) == "configpb.LogConfig.IssuanceChainStorageBackend"
	t.Check("descriptor-names-and-kinds", ok,
		"field 20 = %v, field 21 = %v", f20, f21)

	t.Done()
}
