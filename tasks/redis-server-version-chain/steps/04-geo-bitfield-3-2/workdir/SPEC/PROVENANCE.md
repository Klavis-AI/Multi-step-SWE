# Provenance of the files in this workspace

| path | origin |
| --- | --- |
| `SPEC/redis.conf` | `redis.conf` from `github.com/redis/redis` at tag `3.2.13`, unmodified. |
| `SPEC/commands.json` | `commands.json` from `github.com/redis/redis-doc` as of 2016-06-01 (commit `3b1388570d`) — the command table as it stood when this Redis release shipped. |
| `SPEC/redis-doc/cluster-spec.md` | `topics/cluster-spec.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/eval-intro.md` | `commands/eval.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/keyspace.md` | `topics/data-types-intro.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/notifications.md` | `topics/notifications.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/persistence.md` | `topics/persistence.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/protocol.md` | `topics/protocol.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/pubsub.md` | `topics/pubsub.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/replication.md` | `topics/replication.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `SPEC/redis-doc/transactions.md` | `topics/transactions.md` from `github.com/redis/redis-doc` as of 2016-06-01. |
| `tests/`, `runtest` | Redis's own test suite from `github.com/redis/redis` at tag `3.2.13`. A fixed subset of the suite's files is not included in this copy. |
| `SPEC/BUILD_AND_RUN.md` | written for this task: it describes only how the artifact is built, started and driven, never what it must do. |

Everything under `SPEC/` is reference material. The behaviour it describes is
Redis's, not this task's.
