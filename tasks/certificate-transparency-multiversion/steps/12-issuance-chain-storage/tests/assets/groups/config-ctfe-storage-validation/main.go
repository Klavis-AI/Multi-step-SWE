// Observes the validation this release adds to ValidateLogConfig for the two
// new per-log fields, ctfe_storage_connection_string and
// extra_data_issuance_chain_storage_backend, and the two new exported fields
// of ValidatedLogConfig that carry them forward.
//
// SCOPE-DB: no database server runs. Every case is decided by parsing, never
// by connecting -- a well-formed DSN is accepted on its syntax alone, and the
// host it names is never dialled.
package main

import (
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"
	"google.golang.org/protobuf/types/known/anypb"

	"encoding/pem"

	"ctverify/vhelp"
)

const (
	trillianGRPC = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC
	ctfeBackend  = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE
	goodDSN      = "mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe"
)

func baseConfig(t *vhelp.T) *configpb.LogConfig {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("pinned log key fixture is not PEM")
	}
	priv, err := anypb.New(&keyspb.PrivateKey{Der: blk.Bytes})
	if err != nil {
		t.Fatalf("wrap private key: %v", err)
	}
	return &configpb.LogConfig{
		LogId:      1001,
		Prefix:     "bench",
		PrivateKey: priv,
	}
}

func main() {
	t := vhelp.New()

	// A config that names neither field is a Trillian-gRPC log.
	cfg := baseConfig(t)
	v, err := ctfe.ValidateLogConfig(cfg)
	t.Check("default-backend-is-trillian-grpc",
		err == nil && v != nil && v.ExtraDataIssuanceChainStorageBackend == trillianGRPC && v.CTFEStorageConnectionString == "",
		"unset fields validated to (%v, %q, err=%v), want (TRILLIAN_GRPC, \"\", <nil>)", backendOf(v), connOf(v), err)

	// A Trillian-gRPC log ignores any connection string it is given: the
	// string must not be carried into the validated config.
	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = trillianGRPC
	cfg.CtfeStorageConnectionString = "postgres://ctfe@127.0.0.1:5432/ctfe"
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("trillian-grpc-ignores-connection-string",
		err == nil && v != nil && v.CTFEStorageConnectionString == "",
		"TRILLIAN_GRPC with a postgres connection string validated to (%q, err=%v), want (\"\", <nil>)", connOf(v), err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("ctfe-backend-requires-connection-string", v == nil && err != nil,
		"CTFE backend with no connection string = (%v, %v), want (<nil>, <error>)", v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = "postgres://ctfe:letmein@127.0.0.1:5432/ctfe"
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("ctfe-backend-rejects-non-mysql-driver", v == nil && err != nil,
		"CTFE backend with a postgres connection string = (%v, %v), want (<nil>, <error>)", v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = "mysql://this is not a dsn"
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("ctfe-backend-rejects-unparseable-dsn", v == nil && err != nil,
		"CTFE backend with an unparseable mysql DSN = (%v, %v), want (<nil>, <error>)", v, err)

	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = goodDSN
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("ctfe-backend-accepts-and-records-mysql-dsn",
		err == nil && v != nil && v.CTFEStorageConnectionString == goodDSN && v.ExtraDataIssuanceChainStorageBackend == ctfeBackend,
		"CTFE backend with %q validated to (%v, %q, err=%v), want (CTFE, the same string, <nil>)", goodDSN, backendOf(v), connOf(v), err)

	const paramDSN = "mysql://ctfe:letmein@tcp(db.internal:3306)/ctfe?parseTime=true&loc=UTC"
	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = paramDSN
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("ctfe-backend-accepts-dsn-with-parameters",
		err == nil && v != nil && v.CTFEStorageConnectionString == paramDSN,
		"CTFE backend with %q validated to (%q, err=%v)", paramDSN, connOf(v), err)

	// The backend switch has exactly two arms and no default, so a value
	// outside the enum is carried through untouched rather than rejected.
	cfg = baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = configpb.LogConfig_IssuanceChainStorageBackend(7)
	v, err = ctfe.ValidateLogConfig(cfg)
	t.Check("out-of-range-backend-is-carried-through",
		err == nil && v != nil && v.ExtraDataIssuanceChainStorageBackend == configpb.LogConfig_IssuanceChainStorageBackend(7) && v.CTFEStorageConnectionString == "",
		"backend 7 validated to (%v, %q, err=%v), want (7, \"\", <nil>)", backendOf(v), connOf(v), err)

	t.Done()
}

func backendOf(v *ctfe.ValidatedLogConfig) interface{} {
	if v == nil {
		return "<nil config>"
	}
	return int32(v.ExtraDataIssuanceChainStorageBackend)
}

func connOf(v *ctfe.ValidatedLogConfig) string {
	if v == nil {
		return "<nil config>"
	}
	return v.CTFEStorageConnectionString
}
