module ctverify

go 1.16

require (
	github.com/benlaurie/objecthash v0.0.0-20180202135721-d1e3d6079fc1
	github.com/beorn7/perks v0.0.0-20160804104726-4c0e84591b9a
	github.com/coreos/etcd v3.2.15+incompatible
	github.com/go-sql-driver/mysql v1.3.1-0.20180125054745-bc14601d1bd5
	github.com/gogo/protobuf v1.0.1-0.20180223151032-d5bc08a2c4c5
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/mock v1.0.1-0.20180211072722-58cd061d0938
	github.com/golang/protobuf v0.0.0-20180222204658-649500c21ecd
	github.com/google/btree v0.0.0-20180124185431-e89373fe6b4a
	github.com/google/trillian v1.0.7-0.20180223125122-7c253b1ef81d
	github.com/grpc-ecosystem/grpc-gateway v1.3.1-0.20180223033028-21563a15903a
	github.com/juju/ratelimit v1.0.1
	github.com/kylelemons/godebug v0.0.0-20171118190045-7a67e28dea99
	github.com/mattn/go-sqlite3 v1.6.1-0.20180218131552-696e2e43cb00
	github.com/matttproud/golang_protobuf_extensions v1.0.1
	github.com/prometheus/client_golang v0.9.0-pre1.0.20180223144718-c3324c1198cf
	github.com/prometheus/client_model v0.0.0-20171117100541-99fa1f4be8e5
	github.com/prometheus/common v0.0.0-20180110214958-89604d197083
	github.com/prometheus/procfs v0.0.0-20180212145926-282c8707aa21
	golang.org/x/crypto v0.0.0-20180222182404-49796115aa4b
	golang.org/x/net v0.0.0-20180218175443-cbe0f9307d01
	golang.org/x/sys v0.0.0-20180223165747-88d2dcc51026
	golang.org/x/text v0.3.1-0.20180223174132-20787f6828fc
	google.golang.org/genproto v0.0.0-20180206005123-2b5a72b8730b
	google.golang.org/grpc v1.2.1-0.20180222222859-583a6303969e
)

require github.com/google/certificate-transparency-go v1.99.0

replace github.com/google/certificate-transparency-go => /app
