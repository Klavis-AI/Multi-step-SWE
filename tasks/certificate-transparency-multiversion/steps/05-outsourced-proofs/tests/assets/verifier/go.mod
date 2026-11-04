module ctverify

go 1.16

require (
	github.com/coreos/etcd v3.3.13+incompatible
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/mock v1.2.0
	github.com/golang/protobuf v1.3.1
	github.com/google/trillian v1.2.0
	github.com/juju/ratelimit v1.0.1
	github.com/kylelemons/godebug v1.1.0
	github.com/mattn/go-sqlite3 v1.9.0
	github.com/prometheus/client_golang v0.9.4
	github.com/rs/cors v1.5.0
	github.com/stretchr/testify v1.3.0
	golang.org/x/crypto v0.0.0-20190313024323-a1f597ede03a
	golang.org/x/net v0.0.0-20190620200207-3b0461eec859
	google.golang.org/genproto v0.0.0-20190605220351-eb0b1bdb6ae6
	google.golang.org/grpc v1.20.1
)

require (
	contrib.go.opencensus.io/exporter/stackdriver v0.12.1 // indirect
	github.com/coreos/bbolt v1.3.2 // indirect
	github.com/coreos/go-semver v0.3.0 // indirect
	github.com/coreos/go-systemd v0.0.0-20190321100706-95778dfbb74e // indirect
	github.com/coreos/pkg v0.0.0-20180928190104-399ea9e2e55f // indirect
	github.com/dgrijalva/jwt-go v3.2.0+incompatible // indirect
	github.com/go-sql-driver/mysql v1.4.0 // indirect
	github.com/golang/groupcache v0.0.0-20190129154638-5b532d6fd5ef // indirect
	github.com/google/btree v1.0.0 // indirect
	github.com/gorilla/websocket v1.4.0 // indirect
	github.com/grpc-ecosystem/go-grpc-middleware v1.0.0 // indirect
	github.com/grpc-ecosystem/go-grpc-prometheus v1.2.0 // indirect
	github.com/grpc-ecosystem/grpc-gateway v1.9.0 // indirect
	github.com/jonboulle/clockwork v0.1.0 // indirect
	// TODO(gbelvin): Remove when Trillian declares its dependencies with go.mod
	github.com/letsencrypt/pkcs11key v2.0.1-0.20170608213348-396559074696+incompatible // indirect
	github.com/miekg/pkcs11 v1.0.2 // indirect
	github.com/soheilhy/cmux v0.1.4 // indirect
	github.com/tmc/grpc-websocket-proxy v0.0.0-20190109142713-0ad062ec5ee5 // indirect
	github.com/xiang90/probing v0.0.0-20190116061207-43a291ad63a2 // indirect
	go.uber.org/atomic v1.4.0 // indirect
	go.uber.org/multierr v1.1.0 // indirect
	go.uber.org/zap v1.10.0 // indirect
	google.golang.org/api v0.6.0 // indirect
	gopkg.in/yaml.v2 v2.2.2 // indirect
)

require github.com/google/certificate-transparency-go v1.99.0

replace github.com/google/certificate-transparency-go => /app
