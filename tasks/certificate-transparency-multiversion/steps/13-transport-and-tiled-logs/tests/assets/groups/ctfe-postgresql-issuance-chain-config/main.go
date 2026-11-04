// ctfe-postgresql-issuance-chain-config
//
// v1.3.3 gives CTFE a second issuance-chain database dialect. Two places
// change together: ValidateLogConfig, whose single "must start with mysql"
// test becomes a two-armed one that validates a postgres connection string
// with the pgx driver's own parser, and storage.NewIssuanceChainStorage,
// whose CTFE arm now returns a *postgresql.IssuanceChainStorage when the
// connection string names postgres. This group observes both, and pins the
// mysql arm and the Trillian-gRPC arm alongside them so that the second
// dialect is added rather than swapped in.
//
// SCOPE-DB: no database server runs. Every verdict here is reached by
// parsing: sql.Open is lazy for the pgx stdlib driver, so constructing the
// store contacts nothing, and no DSN named below is ever dialled. The
// well-formed *mysql* constructor arm is deliberately not exercised, because
// that one does reach for a server as it opens.
package main

import (
	"context"
	"encoding/pem"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/certificate-transparency-go/trillian/ctfe/storage"
	"github.com/google/certificate-transparency-go/trillian/ctfe/storage/postgresql"
	"github.com/google/trillian/crypto/keyspb"
	"google.golang.org/protobuf/types/known/anypb"

	"ctverify/vhelp"
)

const (
	trillianGRPC = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC
	ctfeBackend  = configpb.LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE

	pgLongDSN  = "postgresql://ctfe:letmein@127.0.0.1:5432/ctfe"
	pgShortDSN = "postgres://ctfe:letmein@127.0.0.1:5432/ctfe"
	pgParamDSN = "postgresql://ctfe:letmein@127.0.0.1:5432/ctfe?sslmode=disable&connect_timeout=5"
	pgBadDSN   = "postgresql://ctfe:letmein@127.0.0.1:not-a-port/ctfe"
	myDSN      = "mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe"
	myBadDSN   = "mysql://this is not a dsn"
	otherDSN   = "sqlite://file:/tmp/ctfe.db"

	// CONFIG.md 2.6 gives the mysql refusal in full and says v1.3.3 adds
	// "the matching ...for postgresql pgx driver" message. What is graded is
	// what that message identifies and nothing else (TEST_INTERFACE.md 3.3):
	// the config field the bad value came from, and the driver that rejected
	// it. The sentence around those two names is the product's own. The
	// message is the only channel: a nil config and a non-nil error cannot by
	// themselves say which of 2.6's several refusals fired.
	pgParseField  = "ctfe_storage_connection_string"
	pgParseDriver = "postgres"
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
	return &configpb.LogConfig{LogId: 1001, Prefix: "bench", PrivateKey: priv}
}

func validate(t *vhelp.T, dsn string) (*ctfe.ValidatedLogConfig, error) {
	cfg := baseConfig(t)
	cfg.ExtraDataIssuanceChainStorageBackend = ctfeBackend
	cfg.CtfeStorageConnectionString = dsn
	return ctfe.ValidateLogConfig(cfg)
}

func main() {
	t := vhelp.New()
	ctx := context.Background()

	// --- ValidateLogConfig, the new arm ----------------------------------
	v, err := validate(t, pgLongDSN)
	t.Check("config-accepts-a-postgresql-connection-string",
		err == nil && v != nil && v.CTFEStorageConnectionString == pgLongDSN &&
			v.ExtraDataIssuanceChainStorageBackend == ctfeBackend,
		"CTFE backend with %q validated to (%v, err=%v), want it accepted and recorded verbatim", pgLongDSN, v, err)

	v, err = validate(t, pgShortDSN)
	t.Check("config-accepts-the-postgres-scheme-too",
		err == nil && v != nil && v.CTFEStorageConnectionString == pgShortDSN,
		"CTFE backend with %q validated to (%v, err=%v), want it accepted", pgShortDSN, v, err)

	v, err = validate(t, pgParamDSN)
	t.Check("config-accepts-a-postgresql-connection-string-with-parameters",
		err == nil && v != nil && v.CTFEStorageConnectionString == pgParamDSN,
		"CTFE backend with %q validated to (%v, err=%v), want it accepted", pgParamDSN, v, err)

	v, err = validate(t, pgBadDSN)
	t.Check("config-rejects-an-unparseable-postgresql-connection-string",
		v == nil && err != nil &&
			strings.Contains(err.Error(), pgParseField) &&
			strings.Contains(strings.ToLower(err.Error()), pgParseDriver),
		"CTFE backend with %q validated to (%v, %v), want (<nil>, an error naming %q and %q)",
		pgBadDSN, v, err, pgParseField, pgParseDriver)

	// --- ValidateLogConfig, the arms that must not have moved -------------
	v, err = validate(t, myDSN)
	t.Check("config-still-accepts-a-mysql-connection-string",
		err == nil && v != nil && v.CTFEStorageConnectionString == myDSN,
		"CTFE backend with %q validated to (%v, err=%v), want it accepted", myDSN, v, err)

	v, err = validate(t, myBadDSN)
	t.Check("config-still-rejects-an-unparseable-mysql-connection-string",
		v == nil && err != nil,
		"CTFE backend with %q validated to (%v, %v), want (<nil>, <error>)", myBadDSN, v, err)

	v, err = validate(t, otherDSN)
	t.Check("config-still-rejects-a-third-driver",
		v == nil && err != nil,
		"CTFE backend with %q validated to (%v, %v), want (<nil>, <error>)", otherDSN, v, err)

	// --- the constructor --------------------------------------------------
	st, err := storage.NewIssuanceChainStorage(ctx, ctfeBackend, pgLongDSN)
	pg, isPG := st.(*postgresql.IssuanceChainStorage)
	t.Check("constructor-returns-a-postgresql-store",
		err == nil && isPG && pg != nil,
		"NewIssuanceChainStorage(CTFE, %q) = (%T, %v), want (*postgresql.IssuanceChainStorage, <nil>)", pgLongDSN, st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, ctfeBackend, pgShortDSN)
	_, isPG = st.(*postgresql.IssuanceChainStorage)
	t.Check("constructor-accepts-the-postgres-scheme-too",
		err == nil && isPG,
		"NewIssuanceChainStorage(CTFE, %q) = (%T, %v), want (*postgresql.IssuanceChainStorage, <nil>)", pgShortDSN, st, err)

	// The concrete type is usable through the interface the rest of CTFE
	// holds it by; this is a compile-time constraint as well as a run-time
	// one.
	var iface storage.IssuanceChainStorage = pg
	t.Check("postgresql-store-satisfies-the-issuance-chain-interface", iface != nil,
		"a *postgresql.IssuanceChainStorage is not usable as storage.IssuanceChainStorage")

	// The constructor exported by the postgresql package itself has the
	// documented shape.
	direct := postgresql.NewIssuanceChainStorage(ctx, pgParamDSN)
	var iface2 storage.IssuanceChainStorage = direct
	t.Check("the-postgresql-package-constructor-returns-a-usable-store",
		direct != nil && iface2 != nil,
		"postgresql.NewIssuanceChainStorage(%q) = %v", pgParamDSN, direct)

	st, err = storage.NewIssuanceChainStorage(ctx, ctfeBackend, otherDSN)
	t.Check("constructor-still-rejects-a-third-driver", st == nil && err != nil,
		"NewIssuanceChainStorage(CTFE, %q) = (%v, %v), want (<nil>, <error>)", otherDSN, st, err)

	st, err = storage.NewIssuanceChainStorage(ctx, trillianGRPC, pgLongDSN)
	t.Check("trillian-grpc-backend-still-uses-no-storage", st == nil && err == nil,
		"NewIssuanceChainStorage(TRILLIAN_GRPC, <postgres dsn>) = (%v, %v), want (<nil>, <nil>)", st, err)

	t.Done()
}
