# Provenance of the files in this workspace

| path | origin |
| --- | --- |
| `SPEC/redis.conf` | `redis.conf` from `github.com/redis/redis` at tag `6.0.20`, unmodified. |
| `SPEC/commands.json` | `commands.json` from `github.com/redis/redis-doc` as of 2020-05-15 (commit `c0853c162d`) — the command table as it stood when this Redis release shipped. |
| `SPEC/redis-doc/acl.md` | `topics/acl.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/client-side-caching.md` | `topics/client-side-caching.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/cluster-spec.md` | `topics/cluster-spec.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/eval-intro.md` | `commands/eval.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/keyspace.md` | `topics/data-types-intro.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/lru-cache.md` | `topics/lru-cache.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/notifications.md` | `topics/notifications.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/persistence.md` | `topics/persistence.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/protocol.md` | `topics/protocol.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/pubsub.md` | `topics/pubsub.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/replication.md` | `topics/replication.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/resp3-spec.md` | `github.com/antirez/RESP3/spec.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/streams-intro.md` | `topics/streams-intro.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `SPEC/redis-doc/transactions.md` | `topics/transactions.md` from `github.com/redis/redis-doc` as of 2020-05-15. |
| `tests/`, `runtest` | Redis's own test suite from `github.com/redis/redis` at tag `6.0.20`. A fixed subset of the suite's files is not included in this copy. |
| `SPEC/BUILD_AND_RUN.md` | written for this task: it describes only how the artifact is built, started and driven, never what it must do. |

Everything under `SPEC/` is reference material. The behaviour it describes is
Redis's, not this task's.
