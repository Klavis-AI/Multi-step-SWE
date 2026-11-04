module ctverify

go 1.16

require (
	github.com/benlaurie/objecthash v0.0.0-20171111221500-e06914fda8f8
	github.com/beorn7/perks v0.0.0-20160804104726-4c0e84591b9a
	github.com/coreos/etcd v3.2.10+incompatible
	github.com/go-sql-driver/mysql v1.3.1-0.20171117115124-cd4cb909ce1a
	github.com/gogo/protobuf v0.0.0-20171123125729-971cbfd2e72b
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/mock v1.0.1-0.20171121184817-747e9e6390e1
	github.com/golang/protobuf v0.0.0-20171120103945-013f295b1c74
	github.com/google/trillian v1.0.4-0.20171121150315-bc4e6834483a
	github.com/grpc-ecosystem/grpc-gateway v1.2.2
	github.com/juju/ratelimit v1.0.1
	github.com/kylelemons/godebug v0.0.0-20171118190045-7a67e28dea99
	github.com/mattn/go-sqlite3 v1.5.0
	github.com/matttproud/golang_protobuf_extensions v1.0.1
	github.com/prometheus/client_golang v0.9.0-pre1.0.20171124121253-3c0e2b3c2e52
	github.com/prometheus/client_model v0.0.0-20171117100541-99fa1f4be8e5
	github.com/prometheus/common v0.0.0-20171117163051-2e54d0b93cba
	github.com/prometheus/procfs v0.0.0-20171017214025-a6e9df898b13
	golang.org/x/crypto v0.0.0-20171124090802-b080dc9a8c48
	golang.org/x/net v0.0.0-20171123081856-c7086645de24
	golang.org/x/sys v0.0.0-20171123182949-a13efeb2fd21
	golang.org/x/text v0.1.1-0.20171102192421-88f656faf3f3
	google.golang.org/genproto v0.0.0-20171123000638-7f0da29060c6
	google.golang.org/grpc v1.7.3
)

require github.com/google/certificate-transparency-go v1.99.0

replace github.com/google/certificate-transparency-go => /app
